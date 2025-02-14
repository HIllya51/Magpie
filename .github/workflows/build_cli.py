import os, subprocess, shutil

programFilesX86Path = os.environ["ProgramFiles(x86)"]
vswherePath = programFilesX86Path + "\\Microsoft Visual Studio\\Installer\\vswhere.exe"
if not os.access(vswherePath, os.X_OK):
    raise Exception("未找到 vswhere")

p = subprocess.run(
    vswherePath
    + " -latest -requires Microsoft.Component.MSBuild -find MSBuild\\**\\Bin\\MSBuild.exe",
    capture_output=True,
)
msbuildPath = str(p.stdout, encoding="utf-8").splitlines()[0]
if not os.access(msbuildPath, os.X_OK):
    raise Exception("未找到 msbuild")


subprocess.run(
    f'"{msbuildPath}" -restore -p:RestorePackagesConfig=true;Configuration=Release;Platform=x64;OutDir={os.getcwd()}\\publish\\x64\\ -t:Magpie_Core;Effects Magpie.sln'
)
os.makedirs(f"ALL/Magpie", exist_ok=True)
shutil.move("publish/x64/Magpie.Core.exe", f"ALL/Magpie")
shutil.move("publish/x64/effects", f"ALL/Magpie")
os.system(rf'"C:\Program Files\7-Zip\7z.exe" a -m0=LZMA -mx9 .\\magpie.zip .\\ALL')
