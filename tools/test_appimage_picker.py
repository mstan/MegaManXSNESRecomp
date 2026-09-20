#!/usr/bin/env python3
"""Exercise the packaged ROM picker through AppRun without opening dialogs.

This tests native selection/cancellation/failure and the fallback decision. The
interactive built-in browser still needs separate UI validation.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('appdir', type=Path)
    args = parser.parse_args()
    appdir = args.appdir.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='mmx-picker-') as scratch:
        scratch = Path(scratch)
        bins = scratch / 'bin'
        bins.mkdir()
        # AppRun needs these commands. Leave every desktop picker off PATH
        # except our stubs so a build never opens a dialog on the host.
        for tool in ('dirname', 'readlink', 'mkdir', 'rm', 'cp', 'chmod', 'head', 'tr'):
            binary = shutil.which(tool)
            assert binary, f'Missing test prerequisite: {tool}'
            (bins / tool).symlink_to(binary)
        env = os.environ.copy()
        for key in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'LD_AUDIT', 'RECOMP_HOST_LD_LIBRARY_PATH',
                    'RECOMP_UI_BUILTIN_FILE_PICKER', 'SNESRECOMP_NO_LAUNCHER',
                    'DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
            env.pop(key, None)
        env.update(PATH=str(bins), APPDIR=str(appdir),
                   SDL_VIDEODRIVER='offscreen', SDL_VIDEO_DRIVER='offscreen',
                   SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
                   RECOMP_UI_PICKER_SELFTEST='1', XDG_SESSION_DESKTOP='KDE',
                   XDG_CURRENT_DESKTOP='KDE')

        outcomes = [('selected', 0, 1), ('cancelled', 1, 0),
                    ('failed', 2, -1), ('loader-failed', 127, -1)]
        cases = [(f'{backend}-{name}', backend, code, expected)
                 for backend in ('kdialog', 'zenity')
                 for name, code, expected in outcomes]
        cases.append(('missing', None, None, -1))
        for name, backend, exit_code, expected in cases:
            run = scratch / name
            run.mkdir()
            recorded = run / 'backend.env'
            selected = run / 'selected.sfc'
            for picker in ('kdialog', 'zenity'):
                stub = bins / picker
                if stub.exists():
                    stub.unlink()
                if picker == backend:
                    # The hook returns to normal ROM resolution. Cancel later
                    # invocations so a fake selected path cannot prompt forever.
                    stub.write_text('#!/bin/sh\n'
                                    f'if [ -f "$MMX_PICKER_ENV.{picker}" ]; then exit 1; fi\n'
                                    f': > "$MMX_PICKER_ENV.{picker}"\n'
                                    'if [ ! -f "$MMX_PICKER_ENV" ]; then /usr/bin/env > "$MMX_PICKER_ENV"; fi\n'
                                    'printf "%s\\n" "$MMX_PICKER_SELECTED"\n'
                                    f'exit {exit_code}\n')
                    stub.chmod(0o755)
            env.update(APPIMAGE=str(run / 'MMX.AppImage'),
                       MMX_PICKER_ENV=str(recorded), MMX_PICKER_SELECTED=str(selected))
            # The self-test returns to the host, which exits without a ROM.
            # Its exit status is not the picker result: require the real hook's
            # explicit output, rather than accepting any failed launch.
            try:
                result = subprocess.run([str(appdir / 'AppRun'), '--launcher'],
                                        cwd=run, env=env, capture_output=True,
                                        text=True, timeout=20)
            except subprocess.TimeoutExpired as exc:
                raise AssertionError(f'{name}: packaged picker timed out; '
                                     f'stdout={exc.stdout!r}, stderr={(exc.stderr or b"")[:3000]!r}') from exc
            output = result.stdout + result.stderr
            assert result.returncode in (0, 1), output
            assert f'[picker-selftest] native_available={int(exit_code is not None)}' in output, output
            assert f'[picker-selftest] result={expected} path=[' in output, output
            fallback = 'yes' if expected == -1 else 'no'
            assert f'[picker-selftest] builtin_fallback={fallback}' in output, output
            if expected == 1:
                assert f'path=[{selected}]' in output, output
            if exit_code is not None:
                assert recorded.is_file(), 'Native stub never ran'
                child_env = dict(line.split('=', 1) for line in recorded.read_text().splitlines() if '=' in line)
                for forbidden in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'LD_AUDIT', 'APPDIR', 'APPIMAGE'):
                    assert not child_env.get(forbidden), f'{name}: leaked {forbidden}'
            print(f'PASS: packaged picker {name}: result={expected}, fallback={fallback}')


if __name__ == '__main__':
    main()
