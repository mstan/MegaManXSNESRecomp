"""Cold-boot two real desktop hosts; check pacing and offline-save isolation."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "rom", "x3", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--port", type=int, default=18010)
    parser.add_argument("--savestate-menu", action="store_true",
                        help="Exercise host save/load/cancel and guest authority checks")
    parser.add_argument("--force-mismatch", action="store_true",
                        help="With --savestate-menu: diverge the guest once so the "
                             "host's state and slot must be sent to repair it")
    parser.add_argument("--menu-hold-ms", type=int, default=0,
                        help="With --savestate-menu: time to stay in each menu "
                             "(default 1000); long holds prove a paused match stays connected")
    parser.add_argument("--menu-start", type=int, default=0,
                        help="With --savestate-menu: frame of the first menu (default 60); "
                             "a later frame pauses with title audio playing")
    parser.add_argument("--audio", action="store_true",
                        help="Enable audio (SDL dummy driver), as players run it")
    parser.add_argument("--frames", type=int, default=0, help="Override the frame budget")
    parser.add_argument("--peer-env", action="append", default=[], metavar="SEAT:KEY=VALUE",
                        help="Extra environment for one seat, e.g. 0:RNET_RB_FORCE_MISPREDICT=20 "
                             "(engine validation knobs; repeatable)")
    parser.add_argument("--delay-sync", action="store_true")
    args = parser.parse_args()
    frames = (480 + max(0, args.menu_start - 60)) if args.savestate_menu else 180
    if args.frames:
        frames = args.frames
    for name in ("exe", "rom", "x3"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    root = args.output.resolve()
    catalog = Path(__file__).resolve().parents[1] / "mods/preloaded/packages"
    processes = []
    try:
        for seat in range(2):
            peer = root / f"peer{seat}"
            peer.mkdir(parents=True, exist_ok=True)
            # Never overwrite an existing test/user installation or save.
            if (peer / args.exe.name).exists() or (peer / "saves").exists():
                raise RuntimeError(f"Use a fresh output directory: {peer}")
            exe = peer / args.exe.name
            shutil.copy2(args.exe, exe)
            mods = peer / "mods/preloaded"
            shutil.copytree(catalog, mods / "packages")
            (mods / "state.toml").write_text(
                'format_version = 1\n[[shared_resource]]\n'
                'id = "megaman-x.source.x3"\npath = ' +
                json.dumps(str(args.x3), ensure_ascii=False) + '\n', encoding="utf-8")
            (peer / "config.ini").write_text(
                '[General]\nAutosave=1\nDisableFrameDelay=0\n'
                '[Graphics]\nOutputMethod=SDL-Software\nWindowScale=1\n'
                '[Sound]\nEnableAudio=' + ('1' if args.audio else '0') + '\n', encoding="utf-8")
            env = {k: v for k, v in os.environ.items() if not k.startswith(
                ("MMX_", "SNES_NET", "SNES_RB_", "SNESRECOMP_", "RNET_", "LNG_"))}
            env.update(SNES_NETPLAY="1", SNES_NET_SLOT=str(seat),
                SNES_NET_BIND=f"127.0.0.1:{args.port + seat}",
                SNES_NET_PEER=f"127.0.0.1:{args.port + 1 - seat}",
                SNES_NET_INPUT_PLAYER="0", SNESRECOMP_RUN_FRAMES=str(frames),
                SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
            if args.savestate_menu:
                env["SNES_NET_MENU_SELFTEST"] = "1"
                env["SNES_NET_MENU_SELFTEST_SHOTS"] = str(peer)
                if args.menu_start:
                    env["SNES_NET_MENU_SELFTEST_START"] = str(args.menu_start)
                if args.menu_hold_ms:
                    env["SNES_NET_MENU_SELFTEST_HOLD_MS"] = str(args.menu_hold_ms)
                if args.force_mismatch and seat == 1:
                    env["SNES_NET_MENU_FORCE_MISMATCH"] = "1"
            if args.delay_sync:
                env["SNES_NET_MODE"] = "delay"
            for item in args.peer_env:
                where, _, kv = item.partition(":")
                key, _, value = kv.partition("=")
                if int(where) == seat:
                    env[key] = value
            log = (peer / "desktop.log").open("wb")
            proc = subprocess.Popen([str(exe), "--no-launcher", "--rom", str(args.rom)],
                cwd=peer, env=env, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            processes.append((proc, log, peer))
        digests, pauses, saves = [], [], []
        for proc, log, peer in processes:
            result = proc.wait(timeout=40 + 3 * args.menu_hold_ms // 1000 + frames // 15)
            log.close()
            text = (peer / "desktop.log").read_text(errors="replace")
            assert result == 0, text[-4000:]
            digest = re.search(r"RB boot digest agreed \(([0-9a-f]+)\)", text)
            if not args.delay_sync:
                assert digest, text[-4000:]
                digests.append(re.findall(r"RB boot digest agreed \(([0-9a-f]+)\)", text))
            # The first peer to reach the frame budget leaves; the other, a few
            # ticks behind it under the input delay, then sees it go.
            timing = re.search(r"video totals: simulations=(\d+) presentations=\d+ seconds=([0-9.]+)", text)
            assert timing, text[-4000:]
            simulated, elapsed = int(timing.group(1)), float(timing.group(2))
            # The budget counts replayed frames too, so under injected
            # mispredicts the peer that rewinds less is far behind when the
            # other's budget runs out.
            slack = frames if any("FORCE_MISPREDICT" in e for e in args.peer_env) else 10
            assert simulated == frames or (frames - slack <= simulated < frames and
                "netplay barrier requested exit" in text), text[-4000:]
            # A replay that disagrees with the timeline it replaced is a desync.
            assert not re.search(r"RB (POST|BASELINE) FORK", text), text[-4000:]
            assert elapsed >= simulated / 60 - 0.1, f"Guest outran the SNES frame rate: {elapsed}s"
            assert not (peer / "saves/save0.sav").exists(), "Online match wrote an offline autosave"
            assert "match refused" not in text and "INPUT desync" not in text, text[-4000:]
            if args.savestate_menu:
                assert text.count("menu resumed serial=") == 3, text[-6000:]
                assert "menu sync failed" not in text and "RB fork" not in text, text[-6000:]
                # Every pause lands on one tick with one state on both peers.
                pauses.append(re.findall(r"menu paused at tick (\d+) hash=([0-9a-f]{8})", text))
                if args.force_mismatch:
                    # The first pause and the first save diverge on the guest
                    # and are repaired from the host; everything after matches.
                    if peer.name == "peer0":
                        assert text.count("peers MISMATCH - sending host state") == 1, text[-6000:]
                        assert "every peer paused on host state" in text, text[-6000:]
                        assert "save slot=11 verify peers MISMATCH - sending host slot" in text
                    else:
                        assert "menu applied host state at tick" in text, text[-6000:]
                        assert "menu save slot=11 replaced by host copy" in text, text[-6000:]
                        # Its slot is empty at the load: the host's copy is sent and loaded.
                        assert "menu load slot=11 received host copy" in text, text[-6000:]
                        assert "menu load slot=11 applied" in text, text[-6000:]
                else:
                    assert "MISMATCH" not in text, text[-6000:]
                if peer.name == "peer0":
                    assert all(f"action={action}" in text for action in ("save", "load", "cancel"))
                    assert len(re.findall(r"hash=[0-9a-f]{8} peers match", text)) == \
                        (2 if args.force_mismatch else 3), text[-6000:]
                    if not args.force_mismatch:
                        assert "menu save slot=11 verify peers match" in text, text[-6000:]
                    assert "menu load slot=11 applied on every peer" in text, text[-6000:]
                    saves.append(peer / "saves/save11.sav")
                else:
                    assert "guest actions refused" in text
                    assert text.count("guest mirror open") == 3, text[-6000:]
                    assert "menu save slot=11 written locally" in text, text[-6000:]
                    assert "menu load slot=11 applied" in text, text[-6000:]
                    assert not list((peer / "saves").glob("*.sav")), "Guest wrote a personal save"
                    saves.append(peer / "saves/netplay/save11.sav")
            print(f"{peer.name}: {simulated} frames in {elapsed:.3f}s, no autosave")
        if not args.delay_sync:
            assert digests[0] == digests[1], f"Boot/resume states differ: {digests}"
            if args.savestate_menu:
                # Save and cancel resume in the same epoch; the load starts a
                # new one, and so does a pause whose state had to be replaced.
                expected = 3 if args.force_mismatch else 2
                assert len(digests[0]) == expected, f"Unexpected epoch agreements: {digests}"
        if args.savestate_menu:
            assert len(pauses[0]) == 3 and len(pauses[1]) == 3, f"Missing pauses: {pauses}"
            assert [t for t, _ in pauses[0]] == [t for t, _ in pauses[1]], f"Ticks differ: {pauses}"
            first = 1 if args.force_mismatch else 0
            assert pauses[0][first:] == pauses[1][first:], f"Pauses differ: {pauses}"
            blobs = [path.read_bytes() for path in saves]
            assert blobs[0] == blobs[1], "Peers hold different slot 12 states"
            thumbs = [Path(str(path) + ".thumb") for path in saves]
            assert all(t.exists() for t in thumbs), "Missing thumbnail"
            if args.force_mismatch:
                assert thumbs[0].read_bytes() == thumbs[1].read_bytes(), "Host thumbnail not sent"
            print(f"pauses agreed {pauses[0]}; slot 12 identical on both peers ({len(blobs[0])} bytes)")
    finally:
        for proc, log, _ in processes:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            log.close()


if __name__ == "__main__":
    main()
