"""Produce a portable submission bundle, excluding IDE caches and QA renders."""
from pathlib import Path
import hashlib, json, zipfile
root=Path(__file__).resolve().parents[1]
files=[]
for pattern in ['L1pp/*.cpp','L1pp/*.hpp','L1pp/*.vcxproj','L1pp/*.filters',
                'scripts/*.py','scripts/requirements.txt','results/*','pthread/**/*',
                'output/**/*','x64/Release/L1pp.exe','x64/Release/pthreadVC2.dll',
                '*.ps1','CMakeLists.txt','L1pp.slnx','README.md','.gitignore']:
    files.extend(p for p in root.glob(pattern) if p.is_file()
                 and 'qa' not in p.relative_to(root).parts and '__pycache__' not in p.parts)
files=sorted(set(files))
manifest={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
          for p in files if p.name!='manifest.json'}
manifest_file=root/'results/manifest.json'
manifest_file.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
if manifest_file not in files: files.append(manifest_file)
target=root/'L1pp_complete.zip'
with zipfile.ZipFile(target,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=7) as archive:
    for p in files: archive.write(p,p.relative_to(root).as_posix())
with zipfile.ZipFile(target) as archive:
    assert archive.testzip() is None
    names=set(archive.namelist())
    assert {'output/report.pdf','output/report.html','x64/Release/L1pp.exe',
            'x64/Release/pthreadVC2.dll','L1pp/posix.cpp','L1pp/std_thread.cpp'} <= names
print(f'{target}: {len(files)} files, {target.stat().st_size:,} bytes; ZIP verified')
