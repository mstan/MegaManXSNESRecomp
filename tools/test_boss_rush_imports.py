"""Run ROM-backed PR review fixtures in isolated, muted evidence directories."""
from __future__ import annotations
import argparse, json, os, shutil, subprocess, time
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,required=True)
    p.add_argument('--x1',type=Path,required=True)
    p.add_argument('--x2',type=Path,required=True)
    p.add_argument('--x3',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--fixture',choices=['review','sponge','rhino','full','pause','format','audio','death-latch','visual','drops'],default='review')
    p.add_argument('--characters',nargs='+',choices=['x','zero','modern','saber'],default=['x'])
    a=p.parse_args()
    if a.fixture not in ('sponge','rhino') and a.characters!=['x']:
        p.error('Only the sponge and rhino fixtures support the character matrix; other fixtures require --characters x.')
    source=Path(__file__).resolve().parents[1]
    flags={'review':'IMPORT_REVIEW','sponge':'SPONGE','rhino':'RHINO','pause':'PAUSE_VISUAL','format':'STATE_FORMAT','audio':'AUDIO','death-latch':'DEATH_LATCH','visual':'HIT_VISUAL','drops':'DROP'}
    results=[]
    for character in a.characters:
        run=a.output.resolve()/(a.fixture+'-'+character)
        run.mkdir(parents=True,exist_ok=False)
        catalog=run/'mods/preloaded'
        shutil.copytree(source/'mods/preloaded',catalog)
        env=os.environ.copy()
        for key in list(env):
            if key.startswith('MMX_BOSS_RUSH_'): env.pop(key)
        env.update(SDL_AUDIODRIVER='dummy',MMX_REPLAY_RECORD='0',MMX_BOSS_RUSH_TEST='1')
        if a.fixture in ('review','sponge','rhino'):
            env.update(MMX_BOSS_RUSH_RHINO_CHARACTER=character,MMX_BOSS_RUSH_LAUNCHER_ROOT=str(catalog),
                       MMX_BOSS_RUSH_X1_ROM=str(a.x1.resolve()),MMX_BOSS_RUSH_X2_ROM=str(a.x2.resolve()),MMX_BOSS_RUSH_X3_ROM=str(a.x3.resolve()))
        if a.fixture!='full': env['MMX_BOSS_RUSH_'+flags[a.fixture]+'_TEST']='1'
        if a.fixture=='review': env['MMX_BOSS_RUSH_SOURCE_REVIEW_TEST']='1'
        started=time.monotonic()
        with (run/'stdout.log').open('wb') as out,(run/'stderr.log').open('wb') as err:
            result=subprocess.run([str(a.exe.resolve()),str(a.x1.resolve())],cwd=run,env=env,stdout=out,stderr=err)
        entry=dict(fixture=a.fixture,character=character,exit=result.returncode,seconds=round(time.monotonic()-started,2),evidence=str(run))
        results.append(entry)
        print(json.dumps(entry),flush=True)
    (a.output/('results-'+a.fixture+'.json')).write_text(json.dumps(results,indent=2))
    return int(any(r['exit'] for r in results))
if __name__=='__main__':raise SystemExit(main())
