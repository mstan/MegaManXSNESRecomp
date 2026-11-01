"""ROM-backed replay acceptance checks; all runtime files stay in --work-dir."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def check_saber(args, work, env):
    """Real desktop activation, including a lobby/offline option difference."""
    game, rom, x3 = args.game.resolve(), args.rom.resolve(), args.x3_rom.resolve()
    folder = work / "saber-desktop"
    folder.mkdir()
    shutil.copytree(game.parent / "mods/preloaded/packages", folder / "mods/preloaded/packages")
    (folder / "config.ini").write_text(
        "[General]\nAutosave=0\nDisableFrameDelay=0\nSkipLauncher=1\n"
        "[Graphics]\nOutputMethod=SDL-Software\nWidescreen=0\nNewRenderer=1\n"
        "NoSpriteLimits=1\n[Sound]\nEnableAudio=0\n")
    (folder / "mods/preloaded/state.toml").write_text(
        'format_version = 1\n[[shared_resource]]\nid = "megaman-x.source.x3"\n'
        f'path = {json.dumps(x3.as_posix())}\n'
        '[[package]]\nid = "megaman-x.coop"\nversion = "0.0.1"\n'
        '[[feature]]\npackage_id = "megaman-x.coop"\nid = "coop"\nenabled = true\n'
        '[feature.values]\nplayer1 = "zero"\nbehavior = "saber"\ncameras = "independent"\n')
    (folder / "route.script").write_text(
        "wait 1400\npress start 30\nwait 181\npress start 30\nwait 181\npress start 30\n"
        "wait 600\npress right+p2:right+y+p2:y 90\nwait 20\npress b+p2:b 10\n"
        "wait 30\npress x+p2:x 15\nwait 200\nquit\n")
    current = env | {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy",
                     "MMX_REPLAY_DIR": str(folder)}
    current.pop("MMX_REPLAY_RECORD", None)  # Exercise the player default.
    with (folder / "record.log").open("wb") as log:
        result = subprocess.run([str(game), "--rom", str(rom), "--config", str(folder / "config.ini"),
                                 "--no-launcher", "--script", str(folder / "route.script")], cwd=folder, env=current,
                                stdout=log, stderr=subprocess.STDOUT, timeout=120)
    assert result.returncode == 0, (folder / "record.log").read_text(errors="replace")[-4000:]
    assert "SEH CRASH" not in (folder / "record.log").read_text(errors="replace"), \
        "SDL worker threads must be joined before host shutdown"
    capture = sorted(folder.glob("*.replay"))[-1]
    identities = {p.read_text().splitlines()[2] for p in folder.glob("*.replay")}
    assert len(identities) == 1, "source-ROM identity stays stable across character/collision changes"
    assert int(capture.read_text().splitlines()[3].split()[1]) & 32, "Saber must actually activate"
    sidecar = Path(str(capture) + ".mods.toml")
    sidecar.write_text(sidecar.read_text().replace('behavior = "saber"', 'behavior = "x3"')
                       .replace('player1 = "zero"', 'player1 = "x"'))
    helper = Path(__file__).resolve().parents[1] / "tools/replay_diagnostic.py"
    result = subprocess.run([sys.executable, str(helper), str(capture), "--exe", str(game),
                             "--rom", str(rom), "--x3-rom", str(x3), "--work-dir",
                             str(folder / "playback"), "--headless"], cwd=folder, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    (folder / "helper.log").write_bytes(result.stdout)
    assert result.returncode == 0 and b"all recorded checkpoints matched" in result.stdout, \
        result.stdout.decode(errors="replace")[-5000:]
    print("PASS desktop Saber replay with different offline preferences", flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--state-tests", type=Path, required=True)
    p.add_argument("--game", type=Path, required=True)
    p.add_argument("--rom", type=Path, required=True)
    p.add_argument("--assets", type=Path, required=True)
    p.add_argument("--work-dir", type=Path, required=True)
    p.add_argument("--fixture", type=Path, help="optional upgraded gameplay checkpoint for #108")
    p.add_argument("--x3-rom", type=Path, help="also test real desktop Saber activation and active mod options")
    args = p.parse_args()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    env.update(SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
    for key in tuple(env):
        if key.startswith(("MMX_", "SNESRECOMP_RUN", "SNES_NET_")):
            del env[key]
    env["MMX_ZERO_TEST_ASSETS"] = str(args.assets.resolve())
    # The state harness explicitly switches recording on/off to compare the
    # complete machine with an unrecorded control. Keep that control unrecorded.
    env["MMX_REPLAY_RECORD"] = "0"
    command = [str(args.state_tests.resolve()), str(args.rom.resolve())]

    def run(label, settings, good=True, contains=None):
        folder = work / label
        folder.mkdir()
        current = env | settings | {"MMX_REPLAY_DIR": str(folder)}
        result = subprocess.run(command, cwd=folder, env=current, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=120)
        text = result.stdout.decode(errors="replace")
        (folder / "test.log").write_bytes(result.stdout)
        if (result.returncode == 0) != good or (contains and contains not in text):
            raise AssertionError(f"{label}: exit={result.returncode}\n{text[-5000:]}")
        print(f"PASS {label}", flush=True)
        return folder

    for mode in ("solo", "x", "zero", "online", "local"):
        settings = {"MMX_REPLAY_TEST": mode}
        folder = run(f"record-{mode}", settings)
        capture = next(folder.glob("*.replay"))
        if mode == "local":
            assert all(int(line.split()[3], 16) & 128 for line in capture.read_text().splitlines()
                       if line.startswith("I ")), "recordings retain the local paired layout"
        assert sum(line.startswith("S ") for line in capture.read_text().splitlines()) == 3, \
            "capture needs initial, backward rollback and same-frame restore checkpoints"
        run(f"fresh-{mode}", settings | {"MMX_REPLAY_INPUT": str(capture)}, contains="90 executions")
        if mode == "online":
            damaged = work / "bad-view-history.replay"
            lines = capture.read_text().splitlines()
            i = next(i for i, line in enumerate(lines) if line.startswith("H "))
            fields = lines[i].split()
            fields[4] = f"{int(fields[4], 16)^1:08x}"
            lines[i] = " ".join(fields)
            damaged.write_text("\n".join(lines) + "\n")
            shutil.copy2(str(capture) + ".sav", str(damaged) + ".sav")
            run("bad-view-history", settings | {"MMX_REPLAY_INPUT": str(damaged)},
                good=False, contains="FIRST DIVERGENCE")
        if mode != "solo":
            continue
        original = capture.read_text()
        assert original.startswith("MMXREPLAY 3\n"), "new recordings include independent camera history"
        legacy = work / "legacy.replay"
        legacy.write_text(original.replace("MMXREPLAY 3\n", "MMXREPLAY 1\n", 1))
        shutil.copy2(str(capture) + ".sav", str(legacy) + ".sav")
        run("legacy", settings | {"MMX_REPLAY_INPUT": str(legacy)}, contains="90 executions")
        legacy_v2 = work / "legacy-v2.replay"
        legacy_v2.write_text(original.replace("MMXREPLAY 3\n", "MMXREPLAY 2\n", 1))
        shutil.copy2(str(capture) + ".sav", str(legacy_v2) + ".sav")
        run("legacy-v2", settings | {"MMX_REPLAY_INPUT": str(legacy_v2)}, contains="90 executions")
        for label in ("truncated", "bad-hash", "bad-checkpoint", "wrong-rom", "legacy-wrong-rom", "empty"):
            damaged = work / (label + ".replay")
            text = original
            if label == "truncated":
                text = text[:text.rfind("H ")]
            elif label == "bad-hash":
                lines = text.splitlines()
                i = next(i for i, line in enumerate(lines) if line.startswith("H "))
                fields = lines[i].split()
                fields[2] = f"{int(fields[2],16)^1:08x}"
                lines[i] = " ".join(fields)
                text = "\n".join(lines) + "\n"
            elif label in ("wrong-rom", "legacy-wrong-rom"):
                lines = text.splitlines()
                lines[2] = "ROM 00000000"
                if label == "legacy-wrong-rom":
                    lines[0] = "MMXREPLAY 1"
                text = "\n".join(lines) + "\n"
            elif label == "empty":
                text = "\n".join(text.splitlines()[:4]) + "\n"
            damaged.write_text(text)
            state = damaged.with_name(damaged.name + ".sav")
            shutil.copy2(str(capture) + ".sav", state)
            if label == "bad-checkpoint":
                data = bytearray(state.read_bytes())
                data[20] ^= 1
                state.write_bytes(data)
            run(label, settings | {"MMX_REPLAY_INPUT": str(damaged)}, good=False,
                contains="FIRST DIVERGENCE" if label == "bad-hash" else None)
        # Exercise the shipped CLI, host callbacks and settings isolation too.
        helper = Path(__file__).resolve().parents[1] / "tools" / "replay_diagnostic.py"
        result = subprocess.run([sys.executable, str(helper), str(capture),
                                 "--exe", str(args.game.resolve()), "--rom", str(args.rom.resolve()),
                                 "--work-dir", str(work / "desktop"), "--headless"],
                                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        (work / "desktop-test.log").write_bytes(result.stdout)
        if result.returncode or b"all recorded checkpoints matched" not in result.stdout:
            raise AssertionError(result.stdout.decode(errors="replace")[-5000:])
        print("PASS desktop helper", flush=True)
    folder = run("rotation", {"MMX_REPLAY_TEST": "solo", "MMX_REPLAY_ROTATE": "1"})
    captures = sorted(folder.glob("*.replay"))
    assert len(captures) == 8, "the beginning of a longer session must survive the old one-minute cutoff"
    assert len(list(folder.glob("*.replay.sav"))) == 8, "every retained segment needs its checkpoint"
    for i, capture in enumerate(captures):
        run(f"rotated-{i}", {"MMX_REPLAY_TEST": "solo", "MMX_REPLAY_INPUT": str(capture)})
    bounded = run("bounded-history", {"MMX_REPLAY_TEST": "solo", "MMX_REPLAY_ROTATE": "1",
                                      "MMX_REPLAY_HISTORY_MIB": "2"})
    retained = sorted(bounded.glob("*.replay"))
    assert 0 < len(retained) < 8, "checkpoint byte budget must evict whole old segments"
    assert sum(p.stat().st_size for p in bounded.glob("*.replay.sav")) <= 2 * 1024 * 1024
    assert len(retained) == len(list(bounded.glob("*.replay.sav")))
    for i, capture in enumerate(retained):
        run(f"bounded-{i}", {"MMX_REPLAY_TEST": "solo", "MMX_REPLAY_INPUT": str(capture)})
    if args.fixture:
        run("armor", {"MMX_REPLAY_TEST": "armor", "MMX_REPLAY_FIXTURE": str(args.fixture.resolve())})
        settings = {"MMX_REPLAY_TEST": "zero", "MMX_REPLAY_FIXTURE": str(args.fixture.resolve())}
        folder = run("record-gameplay", settings)
        capture = next(folder.glob("*.replay"))
        run("fresh-gameplay", {"MMX_REPLAY_TEST": "zero", "MMX_REPLAY_INPUT": str(capture)})
        settings["MMX_REPLAY_TEST"] = "online"
        folder = run("record-online-gameplay", settings)
        capture = next(folder.glob("*.replay"))
        run("fresh-online-gameplay", {"MMX_REPLAY_TEST": "online", "MMX_REPLAY_INPUT": str(capture)})
    if args.x3_rom:
        check_saber(args, work, env)


if __name__ == "__main__":
    main()
