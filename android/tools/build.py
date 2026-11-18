"""Build a sideload APK with the same NDK/SDL structure as the Sonic ports.

Private generated source paths are accepted through the existing MMX_* and
SNESRECOMP_ROOT environment variables. Signing secrets never enter Gradle files.
"""
import argparse,json,os,subprocess,sys
from pathlib import Path

def main():
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser()
    parser.add_argument('--debug',action='store_true')
    parser.add_argument('--signing',type=Path,help='Private JSON with keystore, alias, store_password, key_password')
    parser.add_argument('--abis',default='arm64-v8a,x86_64')
    args=parser.parse_args()
    env=os.environ.copy()
    env['Python3_EXECUTABLE']=sys.executable
    if not args.debug:
        path=args.signing or (Path(env['MMX_SIGNING_FILE']) if env.get('MMX_SIGNING_FILE') else None)
        if not path:parser.error('A private --signing file is required for a release APK')
        signing=json.loads(path.read_text(encoding='utf-8'))
        env.update(MMX_KEYSTORE=signing['keystore'],MMX_KEY_ALIAS=signing['alias'],
                   MMX_STORE_PASSWORD=signing['store_password'],MMX_KEY_PASSWORD=signing['key_password'])
    subprocess.run([sys.executable,str(root/'tools/fetch_sources.py')],check=True)
    if not (root/'app/src/main/assets/content.zip').is_file():
        parser.error('Stage approved player content with android/tools/stage_content.py')
    wrapper=root/('gradlew.bat' if os.name=='nt' else 'gradlew')
    tasks=['assembleDebug'] if args.debug else ['assembleRelease','lintRelease']
    subprocess.run([str(wrapper),'-p',str(root),*tasks,
                    '-Pabis='+args.abis,'--max-workers=2','--console=plain'],env=env,check=True)

if __name__=='__main__':main()
