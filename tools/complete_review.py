#!/usr/bin/env python3
"""Owner-operated scenario selector and persistent notes for replay review."""
import argparse
import configparser
from datetime import datetime, timezone
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import time


def stamp():
    return datetime.now(timezone.utc).isoformat(timespec='milliseconds')


def atomic_json(path, value):
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value,indent=2),encoding='utf-8')
    temporary.replace(path)


class ReviewGame:
    def __init__(self, root, headless=False):
        self.root=root
        self.manifest=json.loads((root/'manifest.json').read_text())
        self.runtime=root/'runtime';self.runtime.mkdir(exist_ok=True)
        if self.manifest.get('mods_root'):
            mods=Path(self.manifest['mods_root'])
            shutil.copytree(mods,self.runtime/'mods',dirs_exist_ok=True)
        (self.runtime/'saves').mkdir(exist_ok=True)
        (self.runtime/'notes').mkdir(exist_ok=True)
        self.player=root/'player';self.player.mkdir(exist_ok=True)
        source=Path(self.manifest['exe'])
        self.exe=self.player/source.name
        if not self.exe.exists():
            shutil.copy2(source,self.exe)
            for dll in source.parent.glob('*.dll'):shutil.copy2(dll,self.player/dll.name)
            for name in ('assets',):
                if (source.parent/name).exists():shutil.copytree(source.parent/name,self.player/name)
            if (source.parent/'keybinds.ini').exists():shutil.copy2(source.parent/'keybinds.ini',self.player/'keybinds.ini')
        if hashlib.sha256(self.exe.read_bytes()).hexdigest()!=self.manifest['binary_sha256']:
            raise RuntimeError('Review binary differs from the prepared states. Prepare a new review bundle.')
        self.headless=headless;self.process=None;self.sock=None;self.stream=None;self.logs=[]
        self.case=None;self.paused=True

    def command(self, command):
        self.sock.sendall(command.encode('ascii')+b'\n')
        reply=json.loads(self.stream.readline())
        if 'connected' in reply:reply=json.loads(self.stream.readline())
        if 'error' in reply or reply.get('ok') is False:raise RuntimeError(f'{command}: {reply}')
        with (self.runtime/'controller-commands.jsonl').open('a',encoding='utf-8') as f:
            f.write(json.dumps(dict(at=stamp(),command=command,reply=reply))+'\n')
        return reply

    def keyboard_help(self):
        binds=configparser.ConfigParser()
        binds.read(self.player/'keybinds.ini')
        def key(name,fallback):
            value=binds.get('player1',name,fallback=fallback)
            return 'Enter' if value=='Return' else value
        b,y,a,x,l,r,start=(key(n,d) for n,d in
            [('b','Z'),('y','A'),('a','X'),('x','S'),('l','C'),('r','V'),('start','Return')])
        return (f'Keyboard: {b} jump | {y} fire | {a} dash | {start} Start\n'
                f'Practice menu: {l} + {x} + {a} opens | {y} sets | {b} clears\n'
                'Click the game to play. Initial setup is paused. Closing this panel closes its test game.')

    def close(self):
        if self.stream:self.stream.close();self.stream=None
        if self.sock:self.sock.close();self.sock=None
        # Only this selector's disposable child process is ever stopped.
        if self.process and self.process.poll() is None:
            self.process.terminate();self.process.wait(timeout=10)
        self.process=None
        for f in self.logs:f.close()
        self.logs=[]

    def start(self,case,source=1):
        self.close();self.case=case
        state=(self.root/'mods/preloaded/state.toml').read_text()
        if case.get('native_view'):
            state=re.sub(r'(package_id = "megaman-x.enhancement.widescreen"\nid = "widescreen"\n\n?enabled = )true',r'\1false',state)
        (self.runtime/'mods/preloaded/state.toml').write_text(state)
        with socket.socket() as s:
            s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        config=(f'[General]\nAutosave=0\nRunAhead=0\nSkipLauncher=1\nDisableFrameDelay={int(self.headless)}\n'
                '[Graphics]\nFullscreen=0\nWindowSize=960x540\nWindowScale=3\nRenderer=auto\nVSync=1\nWidescreen=1\nDisplayAspect=4:3\nNewRenderer=1\nNoSpriteLimits=1\n'
                f'[Sound]\nEnableAudio=1\nVolume={0 if self.headless else 100}\nAudioFreq=32000\nAudioChannels=2\nAudioSamples=512\n'
                f'[Controller]\nSourceP1={source}\nSourceP2=0\nSourceP3=0\nSourceP4=0\n'
                '[GamepadMap]\nEnableGamepad1=true\nEnableGamepad2=false\nEnableGamepad3=false\nEnableGamepad4=false\n'
                '[KeyMap]\nPause=Shift+p\nPauseDimmed=p\n[Rewind]\nEnabled=0\n')
        (self.runtime/'config.ini').write_text(config,encoding='utf-8')
        env=os.environ.copy()
        for key in list(env):
            if key.startswith(('SDL_','SM_','MMX_','SNESRECOMP_')):env.pop(key)
        env.update(MMX_REPLAY_RECORD='0',SNESRECOMP_DEBUG_PORT=str(port))
        if self.headless:
            env.update(SDL_VIDEODRIVER='dummy',SDL_VIDEO_DRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_AUDIO_DRIVER='dummy')
        run=case['id']+'-'+datetime.now().strftime('%H%M%S')
        self.logs=[(self.runtime/(run+'-stdout.log')).open('wb'),(self.runtime/(run+'-stderr.log')).open('wb')]
        self.process=subprocess.Popen([str(self.exe),'--config',str(self.runtime/'config.ini'),'--paused','--no-launcher',self.manifest['rom']],
            cwd=self.runtime,env=env,stdout=self.logs[0],stderr=self.logs[1],creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.paused=True
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            if self.process.poll() is not None:raise RuntimeError('Game exited; see runtime logs.')
            try:self.sock=socket.create_connection(('127.0.0.1',port),timeout=.5);break
            except OSError:time.sleep(.1)
        if not self.sock:raise RuntimeError('Game debug connection did not open.')
        self.sock.settimeout(15);self.stream=self.sock.makefile('rb')
        self.command('frame');self.reset()
        atomic_json(self.root/'session.json',dict(pid=self.process.pid,port=port,scenario=case['id'],paused=True,started=stamp()))

    def reset(self):
        self.pause();self.command('set_controller 0')
        slot=self.case['slot']
        shutil.copyfile(self.root/f"states/{self.case['state']}.sav",self.runtime/f'saves/save{slot}.sav')
        self.command(f'loadstate {slot}')
        result=self.command('step 3')
        if result.get('timeout'):raise RuntimeError('Scenario load stalled.')
        state=self.state()
        self.command('clear_controller')
        override=self.command('get_controller')
        if override.get('p1_active') or override.get('p2_active'):
            raise RuntimeError(f'Input override was not released: {override}')
        return dict(state=state,override=override)

    def state(self):
        data=bytes.fromhex(self.command('read_ram 0 8192')['hex'])
        def word(a):return struct.unpack_from('<H',data,a)[0]
        return dict(stage=data[0x1f7a],game=list(data[0xd1:0xd5]),x=word(0xbad),y=word(0xbb0),
                    camera=[word(0x1e4d),word(0x1e50)],health=data[0xbcf],frame=self.command('frame'))

    def play(self):
        self.command('clear_controller');result=self.command('continue');self.paused=False
        return result

    def pause(self):
        # Stop at the next frame boundary for owner interaction. The runner's
        # plain pause command is intentionally a no-op for ring-query tooling.
        if self.paused:return self.command('frame')
        result=self.command('step 1')
        if result.get('timeout'):raise RuntimeError('Could not stop at a frame boundary.')
        self.paused=True
        return result

    def capture(self,prefix):
        relative='notes/'+prefix+'.bmp'
        self.command('screenshot '+relative)
        state=self.state()
        return dict(screenshot='runtime/'+relative,state=state)


def run_ui(root):
    import tkinter as tk
    from tkinter import ttk,messagebox
    game=ReviewGame(root)
    path=root/'observations.json'
    notes=json.loads(path.read_text()) if path.exists() else dict(version=1,binary_sha256=game.manifest['binary_sha256'],scenarios={})
    window=tk.Tk();window.title('Mega Man X - Complete Edition review')
    window.geometry(f'740x820+{max(20,window.winfo_screenwidth()-780)}+30');window.minsize(700,740)
    style=ttk.Style();style.configure('Title.TLabel',font=('Segoe UI',15,'bold'))
    body=ttk.Frame(window,padding=16);body.pack(fill='both',expand=True)
    ttk.Label(body,text='Complete Edition feature review',style='Title.TLabel').pack(anchor='w')
    ttk.Label(body,text='Independent setups. Loading or restarting resets only this private test game.').pack(anchor='w',pady=(4,8))
    listing=tk.Listbox(body,height=9,font=('Segoe UI',11),exportselection=False)
    listing.pack(fill='x')
    for case in game.manifest['scenarios']:listing.insert('end',case['title'])
    listing.selection_set(0)
    controls=ttk.Frame(body);controls.pack(fill='x',pady=10)
    source=tk.StringVar(value=game.manifest.get('input','Controller'))
    ttk.Label(controls,text='Input:').pack(side='left')
    input_choice=ttk.Combobox(controls,textvariable=source,values=['Keyboard','Controller'],state='readonly',width=12)
    input_choice.pack(side='left',padx=(5,12))
    status=tk.StringVar(value='Preparing first scenario...')
    title=tk.StringVar();steps=tk.StringVar();expected=tk.StringVar();clip=tk.StringVar()
    ttk.Label(body,textvariable=title,font=('Segoe UI',12,'bold'),wraplength=670).pack(anchor='w',pady=(3,5))
    ttk.Label(body,textvariable=steps,wraplength=670,justify='left').pack(anchor='w',pady=4)
    ttk.Label(body,textvariable=expected,wraplength=670,justify='left').pack(anchor='w',pady=4)
    ttk.Label(body,textvariable=clip,foreground='#555555',wraplength=670).pack(anchor='w',pady=5)
    ttk.Separator(body).pack(fill='x',pady=7)
    ttk.Label(body,text='Your notes - what happened, where, and the movement that triggered it:').pack(anchor='w')
    text=tk.Text(body,height=9,font=('Segoe UI',11),wrap='word',undo=True);text.pack(fill='both',expand=True,pady=6)
    verdict=ttk.Frame(body);verdict.pack(fill='x',pady=5)
    history=tk.StringVar()
    ttk.Label(body,textvariable=history,wraplength=670).pack(anchor='w',pady=4)
    ttk.Label(body,textvariable=status,foreground='#174e7a',wraplength=670).pack(anchor='w',pady=5)
    ttk.Label(body,text=game.keyboard_help(),wraplength=670,foreground='#555555').pack(anchor='w',pady=5)
    current=[None]

    def save_draft():
        if current[0]:
            row=notes['scenarios'].setdefault(current[0]['id'],dict(observations=[]))
            value=text.get('1.0','end-1c')
            if row.get('draft')!=value:
                row['draft']=value;notes['updated']=stamp();atomic_json(path,notes)

    def describe(case):
        title.set(case['title']);steps.set(case['steps']);expected.set(case['expected'])
        clip.set(case.get('setup','All six Complete Edition mods enabled. Adaptive Fit to screen.'))
        row=notes['scenarios'].get(case['id'],{});events=row.get('observations',[])
        history.set('Not reviewed yet.' if not events else 'Saved observations: '+str(len(events))+' | Latest: '+events[-1]['status']+' | Earlier failures remain in history.')

    def safe(action):
        try:action()
        except Exception as e:status.set(str(e));messagebox.showerror('Review setup',str(e))

    def load():
        save_draft();case=game.manifest['scenarios'][listing.curselection()[0]]
        status.set('Loading '+case['id']+'...');window.update_idletasks()
        game.start(case,2 if source.get()=='Controller' else 1)
        current[0]=case;text.delete('1.0','end');text.insert('1.0',notes['scenarios'].get(case['id'],{}).get('draft',''))
        describe(case);status.set('Paused and ready. Click Play, then click the game window.')

    def play():
        game.play();status.set('Playing. Use Pause here to freeze a problem; Save screenshot + note records the current view.')

    def pause():
        game.pause();status.set('Paused. Write your observation, or click Play to continue.')

    def restart():
        save_draft();game.reset();status.set('Scenario restored and paused. Click Play to retry.')

    def record(value):
        save_draft();case=current[0]
        game.pause()
        prefix=datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-'+case['id']
        evidence=game.capture(prefix)
        row=notes['scenarios'].setdefault(case['id'],dict(observations=[]))
        row['observations'].append(dict(at=stamp(),status=value,note=text.get('1.0','end-1c'),**evidence))
        notes['updated']=stamp();atomic_json(path,notes);describe(case)
        status.set('Paused. Saved '+value+' with screenshot. Notes: observations.json')

    ttk.Button(controls,text='Load selected (paused)',command=lambda:safe(load)).pack(side='left',padx=3)
    ttk.Button(controls,text='Restart',command=lambda:safe(restart)).pack(side='left',padx=3)
    ttk.Button(controls,text='Play',command=lambda:safe(play)).pack(side='left',padx=3)
    ttk.Button(controls,text='Pause',command=lambda:safe(pause)).pack(side='left',padx=3)
    input_choice.bind('<<ComboboxSelected>>',lambda event:safe(load))
    for label,value in [('Bug found','Bug found'),('Pass (reviewed)','Pass'),('Defer','Deferred'),('Save screenshot + note','Note')]:
        ttk.Button(verdict,text=label,command=lambda value=value:safe(lambda:record(value))).pack(side='left',padx=(0,6))
    def autosave():
        save_draft();window.after(3000,autosave)
    def close():
        save_draft();game.close();window.destroy()
    window.protocol('WM_DELETE_WINDOW',close)
    window.after(100,lambda:safe(load));window.after(3000,autosave)
    try:window.mainloop()
    finally:game.close()


def verify(root):
    game=ReviewGame(root,headless=True);results=[]
    try:
        for case in game.manifest['scenarios']:
            game.start(case);before=game.state()
            game.command('screenshot '+case['id']+'-selector.bmp')
            game.command('set_controller right');step=game.command('step 2');game.command('clear_controller')
            after=game.state()
            assert after['frame']['frame']>before['frame']['frame'],(before,after)
            game.play();time.sleep(.05);game.pause();time.sleep(.05)
            stopped=game.command('frame');time.sleep(.05)
            assert game.command('frame')['frame']==stopped['frame']
            game.pause();assert game.command('frame')['frame']==stopped['frame']
            evidence=game.capture('selector-check-'+case['id'])
            results.append(dict(id=case['id'],before=before,after=after,step=step,controller=game.command('get_controller'),evidence=evidence))
            print(case['id']+' selector load/input/release passed',flush=True)
        # Exercise the same atomic notes format without marking owner acceptance.
        check=root/'selector-storage-check.json'
        atomic_json(check,dict(status='Automated storage check, not owner review',results=results))
        assert json.loads(check.read_text())['results']==results
    finally:game.close()


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',required=True,type=Path);p.add_argument('--verify',action='store_true')
    a=p.parse_args();verify(a.root.resolve()) if a.verify else run_ui(a.root.resolve())
