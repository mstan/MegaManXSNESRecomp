"""Bundle tracked mod manifests and notices; no ROMs, saves or private caches."""
from pathlib import Path
import subprocess,zipfile
root=Path(__file__).resolve().parents[2]
output=root/'android/app/src/main/assets/content.zip'
output.parent.mkdir(parents=True,exist_ok=True)
files=subprocess.check_output(['git','-C',str(root),'ls-files','mods/preloaded'],text=True).splitlines()
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as z:
 for name in files:
  if Path(name).suffix.lower() in ('.sfc','.smc','.sav','.srm','.bin') or name.endswith('state.toml'):
   raise RuntimeError('Player/generated data in tracked catalog: '+name)
  z.write(root/name,name)
 for name in ('LICENSE','PERMISSIONS.md','assets/saber-zero/CREDITS.md'):
  if (root/name).is_file(): z.write(root/name,'licenses/'+Path(name).name)
print(output)
