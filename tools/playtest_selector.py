"""Run prepared desktop tests and retain individual human observations.

The private manifest supplies a build, its mod selections and current-build
save slots. No ROMs, extracted assets or personal observations belong here.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox


class Selector:
    def __init__(self, root, manifest, client_type):
        self.root, self.path, self.client_type = root, manifest, client_type
        self.data = json.loads(manifest.read_text(encoding="utf-8"))
        self.directory = manifest.parent
        self.notes_path = self.directory / "observations.json"
        self.notes = json.loads(self.notes_path.read_text()) if self.notes_path.exists() else {"results": {}, "history": []}
        self.process = self.client = self.log = None
        self.current = None
        self.selected = self.data["cases"][0]
        self.busy = False
        self.closing = False
        self.loading_notes = False
        self.pending_save = None
        root.title(self.data.get("title", "Mega Man X — Armor Backport Playtest"))
        root.geometry("720x600")
        root.minsize(640, 540)
        body = ttk.Frame(root, padding=16)
        body.pack(fill="both", expand=True)
        ttk.Label(body, text="Armor backport playtest", font=("Segoe UI", 16, "bold")).pack(anchor="w")
        ttk.Label(body, text="Practice saves • P1: X / controller • P2: Saber Zero / keyboard").pack(anchor="w", pady=(4, 12))
        self.choice = ttk.Combobox(body, state="readonly", values=[c["label"] for c in self.data["cases"]])
        self.choice.current(0)
        self.choice.pack(fill="x")
        self.choice.bind("<<ComboboxSelected>>", self.change)
        row = ttk.Frame(body)
        row.pack(fill="x", pady=10)
        self.load_button = ttk.Button(row, text="Load / restart selected test", command=self.load)
        self.load_button.pack(side="left")
        self.play_button = ttk.Button(row, text="Play", command=self.play, state="disabled")
        self.play_button.pack(side="left", padx=8)
        ttk.Label(body, text="Shift+P pauses/resumes in the game. F7 saves/loads; F8 rewinds.").pack(anchor="w")
        self.status = tk.StringVar(value="Choose a setup, then load it. Every setup opens paused.")
        ttk.Label(body, textvariable=self.status, wraplength=680).pack(anchor="w", pady=8)
        self.instructions = ttk.Label(body, wraplength=680, justify="left")
        self.instructions.pack(fill="x", pady=(0, 10))
        self.result = tk.StringVar()
        ttk.Label(body, textvariable=self.result).pack(anchor="w")
        self.text = tk.Text(body, height=6, wrap="word", font=("Segoe UI", 10))
        self.text.pack(fill="both", expand=True, pady=6)
        self.text.bind("<<Modified>>", self.edited)
        buttons = ttk.Frame(body)
        buttons.pack(fill="x")
        self.mark_buttons = []
        for label in ("Pass", "Fail", "Deferred"):
            button = ttk.Button(buttons, text=label, command=lambda s=label: self.mark(s))
            button.pack(side="left", padx=(0, 8))
            self.mark_buttons.append(button)
        ttk.Button(buttons, text="Save notes", command=self.save_notes).pack(side="right")
        caveats = "Remaining checks: " + " ".join(self.data.get("caveats", []))
        ttk.Label(body, text=caveats, wraplength=680, justify="left").pack(fill="x", pady=(12, 0))
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.show_case()

    def show_case(self):
        record = self.notes["results"].get(self.selected["id"], {})
        self.instructions.config(text=self.selected["steps"])
        self.result.set("Observation: " + record.get("status", "Not checked"))
        self.loading_notes = True
        self.text.delete("1.0", "end")
        self.text.insert("1.0", record.get("notes", ""))
        self.text.edit_modified(False)
        self.loading_notes = False
        active = not self.busy and self.current == self.selected["id"]
        for button in self.mark_buttons:
            button.config(state="normal" if active else "disabled")

    def edited(self, event=None):
        if self.loading_notes or not self.text.edit_modified():
            return
        self.text.edit_modified(False)
        if self.pending_save:
            self.root.after_cancel(self.pending_save)
        self.pending_save = self.root.after(500, self.save_notes)

    def save_notes(self):
        self.pending_save = None
        key = self.selected["id"]
        record = self.notes["results"].setdefault(key, {"status": "Not checked"})
        record.update(notes=self.text.get("1.0", "end").strip(), source=self.data["source"], exe_sha256=self.data["exe_sha256"])
        temporary = self.notes_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(self.notes, indent=2), encoding="utf-8")
        temporary.replace(self.notes_path)

    def mark(self, status):
        self.save_notes()
        key = self.selected["id"]
        record = self.notes["results"][key]
        record["status"] = status
        self.notes["history"].append(dict(case=key, status=status, notes=record["notes"],
                                         time=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                                         source=self.data["source"]))
        self.save_notes()
        self.result.set("Observation: " + status)

    def change(self, event=None):
        self.save_notes()
        self.selected = self.data["cases"][self.choice.current()]
        self.show_case()

    def query(self, command):
        reply = self.client.query(command, retry=False)
        if "error" in reply:
            raise RuntimeError(reply["error"])
        return reply

    def stop_game(self):
        if self.process and self.process.poll() is None:
            self.query("game armor close")
            try:
                self.query("step 1")
            except (ConnectionError, OSError):
                pass  # Normal shutdown can close the socket before its reply.
            self.process.wait(timeout=15)
        if self.client:
            self.client.close()
            self.client = None
        if self.log:
            self.log.close()
            self.log = None

    def load(self):
        if self.busy:
            return
        self.save_notes()
        case = self.selected.copy()
        self.busy = True
        self.load_button.config(state="disabled")
        self.play_button.config(state="disabled")
        self.choice.config(state="disabled")
        self.show_case()
        self.status.set("Loading " + case["label"] + "…")
        threading.Thread(target=self.load_worker, args=(case,), daemon=True).start()

    def load_worker(self, case):
        error = None
        try:
            self.stop_game()
            exe = Path(self.data["exe"])
            if hashlib.sha256(exe.read_bytes()).hexdigest() != self.data["exe_sha256"]:
                raise RuntimeError("The prepared executable changed. Rebuild the practice setups first.")
            state = self.directory / f"saves/save{case['slot']}.sav"
            fixture = self.directory / case.get("fixture", f"saves/save{case['slot']}.sav")
            if hashlib.sha256(fixture.read_bytes()).hexdigest() != case["state_sha256"]:
                raise RuntimeError("This practice slot was replaced. Restore its prepared copy first.")
            if fixture != state:
                if state.exists() and hashlib.sha256(state.read_bytes()).hexdigest() != case["state_sha256"]:
                    # Retain an owner's useful F7 snapshot before restarting.
                    archive = self.directory / "recorded-states"
                    archive.mkdir(exist_ok=True)
                    digest = hashlib.sha256(state.read_bytes()).hexdigest()[:16]
                    saved = archive / (case["id"] + "-" + digest + ".sav")
                    shutil.copy2(state, saved)
                    thumbnail = Path(str(state) + ".thumb")
                    if thumbnail.exists():
                        shutil.copy2(thumbnail, Path(str(saved) + ".thumb"))
                shutil.copy2(fixture, state)
            (self.directory / "mods/preloaded/state.toml").write_text(case["mods"], encoding="utf-8")
            drivers = ("SDL_VIDEO_DRIVER", "SDL_VIDEODRIVER", "SDL_AUDIO_DRIVER", "SDL_AUDIODRIVER")
            env = {k: v for k, v in os.environ.items() if not k.startswith(
                ("SNES_NET", "SNES_RB_", "SNESRECOMP_", "RNET_", "MMX_", "LNG_"))
                and not (k in drivers and v.lower() == "dummy")}
            env.update(SNESRECOMP_DEBUG_PORT=str(self.data["port"]), MMX_REPLAY_RECORD="0", SNESRECOMP_RUNAHEAD="0")
            self.log = (self.directory / "owner-runtime.log").open("wb")
            self.process = subprocess.Popen([str(exe), "--config", str(self.directory / "config.ini"),
                                             "--no-launcher", "--paused", "--rom", self.data["rom"]],
                                            cwd=self.directory, env=env, stdout=self.log, stderr=subprocess.STDOUT,
                                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            self.client = self.client_type(self.data["port"], timeout=15)
            deadline = time.monotonic() + 30
            while not self.client.is_alive():
                if self.process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("The game did not start. See owner-runtime.log.")
                time.sleep(.1)
            self.query("loadstate " + str(case["slot"]))
            self.query("step 2")
            # Diagnostics are retained without editing any game/input state.
            diagnostic = {"case": case["id"], "source": self.data["source"]}
            for command in self.data.get("diagnostics", ["game armor", "game armor players"]):
                diagnostic[command] = self.query(command)
            (self.directory / "handoff.json").write_text(json.dumps(diagnostic, indent=2))
            self.query("screenshot " + (self.directory / "handoff.bmp").as_posix())
            self.current = case["id"]
        except Exception as exc:
            error = str(exc)
        self.root.after(0, lambda: self.loaded(case, error))

    def loaded(self, case, error):
        self.busy = False
        if self.closing:
            self.close()
            return
        self.load_button.config(state="normal")
        self.choice.config(state="readonly")
        if error:
            self.status.set(error)
            messagebox.showerror("Could not load practice setup", error)
        else:
            self.status.set("Paused: " + case["label"] + ". Normal audio is enabled.")
            self.play_button.config(state="normal")
        self.show_case()

    def play(self):
        try:
            self.query("continue")
            self.play_button.config(state="disabled")
            self.status.set("Playing. Shift+P pauses in the game; Load restarts the selected setup.")
        except Exception as exc:
            messagebox.showerror("Game control", str(exc))

    def close(self):
        self.save_notes()
        if self.busy:
            self.closing = True
            self.status.set("Closing the practice run after its load finishes…")
            return
        try:
            self.stop_game()
            self.root.destroy()
        except Exception as exc:
            messagebox.showerror("Close the practice game", str(exc))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--framework", type=Path, required=True)
    parser.add_argument("--load-first", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(args.framework / "tools"))
    from sneslib.client import DebugClient
    root = tk.Tk()
    ui = Selector(root, args.manifest.resolve(), DebugClient)
    if args.load_first:
        root.after(100, ui.load)
    root.mainloop()


if __name__ == "__main__":
    main()
