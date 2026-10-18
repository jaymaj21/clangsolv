import argparse
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--checker',required=True)
parser.add_argument('--generated',required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
for source in sorted((root/'examples').glob('*.cpp')):
    result=subprocess.run([args.checker,str(source),'--','-std=c++17',
        '-I'+str(root/'../library/include'),'-I'+str(root/'include'),'-I'+args.generated],
        capture_output=True,text=True,errors='replace')
    if result.returncode==0 or 'missing required labels' not in result.stderr:
        raise SystemExit(f'{source.name}: wrong outcome\n{result.stdout}\n{result.stderr}')
    print(f'{source.name}: correctly rejected')
