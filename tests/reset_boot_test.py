"""Console reset must reboot the game (GitHub #45).

Runs the game headless twice with the engine's input-script `reset` command,
the same path as the Reset hotkey:

  boot   -- no reset; frames BOOT..BOOT+N after power-on
  reset  -- reset before frame R+1 is simulated; frames R+BOOT..R+BOOT+N, the
            same distance after the reset as the boot run's are after power-on

A reset that reboots through I_RESET replays the power-on sequence, so each
reset-run frame must be byte-identical to the boot-run frame at the same
offset. The bug this pins left every frame after a reset black: the host-side
"already booted" latch survived the reset and the game ran its main loop
against a force-blanked PPU.

Needs the ROM and a built executable; CMake registers it only when both can
exist (see CMakeLists.txt).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

RESET_AT = 900   # frames of power-on before the reset
BOOT = 590       # compared window, frames after power-on / after the reset
SPAN = 21


def load_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    header = data.split(b'\n', 3)
    if header[0] != b'P6':
        raise ValueError(f'{path}: not a binary PPM')
    return header[3]   # the pixels; the header carries no frame number


def capture(exe, rom, config, workdir, script_text, first, name, extra_env=None):
    out = os.path.join(workdir, name)
    os.makedirs(out)
    script = os.path.join(workdir, name + '.script')
    with open(script, 'w') as f:
        f.write(script_text)
    env = dict(os.environ)
    env.update({
        'SDL_VIDEODRIVER': 'dummy',
        'SDL_AUDIO_DRIVER': 'dummy',
        'SNESRECOMP_RUN_FRAMES': str(first + SPAN + 5),
        'SNESRECOMP_SCREENSHOT_DIR': out,
        'SNESRECOMP_SCREENSHOT_FROM': str(first),
        'SNESRECOMP_SCREENSHOT_TO': str(first + SPAN - 1),
    })
    env.update(extra_env or {})
    proc = subprocess.run([exe, '--config', config, '--script', script, rom],
                          cwd=workdir, env=env, capture_output=True, text=True,
                          timeout=600)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr[-4000:])
        raise SystemExit(f'{name}: exit code {proc.returncode}')
    frames = sorted(p for p in os.listdir(out) if p.endswith('.ppm'))
    if len(frames) != SPAN:
        raise SystemExit(f'{name}: expected {SPAN} frames, got {len(frames)}')
    return [load_ppm(os.path.join(out, p)) for p in frames], proc.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--rom', required=True)
    ap.add_argument('--config', required=True)
    args = ap.parse_args()

    workdir = tempfile.mkdtemp(prefix='mmx_reset_')
    try:
        config = os.path.join(workdir, 'config.ini')
        shutil.copyfile(args.config, config)
        boot, _ = capture(args.exe, args.rom, config, workdir,
                          'wait 5000\n', BOOT, 'boot')
        # Both schedulers: the default LLE one, and the HLE one whose task
        # fibers the reset hook has to tear down.
        for mode in ('lle', 'hle'):
            reset, log = capture(args.exe, args.rom, config, workdir,
                                 f'wait {RESET_AT}\nreset\n', RESET_AT + BOOT,
                                 'reset_' + mode,
                                 {'SNESRECOMP_EXECUTION_MODE': mode})
            if 'console reset' not in log:
                raise SystemExit(f'{mode}: the script never reached the reset')
            blank = sum(1 for f in reset if not any(f))
            if blank:
                raise SystemExit(f'{mode}: {blank}/{SPAN} frames after the reset '
                                 'are black (the game did not reboot)')
            for i, (a, b) in enumerate(zip(boot, reset)):
                if a != b:
                    raise SystemExit(f'{mode}: frame {i} after the reset differs '
                                     'from the same moment after power-on')
        print(f'ok: {SPAN} frames after a reset match the same frames after '
              'power-on, LLE and HLE')
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == '__main__':
    main()
