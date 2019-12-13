[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

$python_dl_url = 'https://www.python.org/ftp/python/3.7.5/python-3.7.5.exe'
$msvc_2017_url = 'https://aka.ms/vs/15/release/vs_buildtools.exe'
$git_url = 'https://github.com/git-for-windows/git/releases/download/v2.24.1.windows.2/MinGit-2.24.1.2-64-bit.zip'
$zip_url = 'https://www.7-zip.org/a/7z1900-x64.exe'
$cmake_url = 'https://github.com/Kitware/CMake/releases/download/v3.16.1/cmake-3.16.1-win64-x64.msi'
$msys2_url = 'https://download.sourceforge.net/project/msys2/Base/x86_64/msys2-base-x86_64-20190524.tar.xz'
$msys_mingw_get_url = 'https://osdn.net/projects/mingw/downloads/68260/mingw-get-0.6.3-mingw32-pre-20170905-1-bin.tar.xz'

Write-Host "Installing VisualStudio"
Invoke-WebRequest -Uri $msvc_2017_url -OutFile C:\vs_buildtools.exe
Start-Process C:\vs_buildtools.exe -ArgumentList '--quiet --wait --norestart --nocache --installPath C:\BuildTools --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' -Wait
Remove-Item C:\vs_buildtools.exe -Force

Write-Host "Installing Python"
Invoke-WebRequest -Uri $python_dl_url -OutFile C:\python3-installer.exe
Start-Process C:\python3-installer.exe -ArgumentList '/quiet InstallAllUsers=1 PrependPath=1 TargetDir=C:\Python37' -Wait
Remove-Item C:\python3-installer.exe -Force

Write-Host "Installing Git"
Invoke-WebRequest -Uri $git_url -OutFile C:\mingit.zip
Expand-Archive C:\mingit.zip -DestinationPath c:\mingit
Remove-Item C:\mingit.zip -Force
$env:PATH = [System.Environment]::GetEnvironmentVariable('PATH', 'Machine') + ';' + 'c:\mingit\cmd'
[Environment]::SetEnvironmentVariable('PATH', $env:PATH, [EnvironmentVariableTarget]::Machine)

Write-Host "Installing CMake for Cerbero"
Invoke-WebRequest -Uri $cmake_url -OutFile C:\cmake-x64.msi
Start-Process msiexec -ArgumentList '-i C:\cmake-x64.msi -quiet -norestart ADD_CMAKE_TO_PATH=System' -Wait
Remove-Item C:\cmake-x64.msi -Force

Write-Host "Installing 7zip"
Invoke-WebRequest -Uri $zip_url -OutFile C:\7z-x64.exe
Start-Process C:\7z-x64.exe -ArgumentList '/S /D=C:\7zip\' -Wait
Remove-Item C:\7z-x64.exe -Force

Write-Host "Downloading and extracting mingw-get for MSYS"
Invoke-WebRequest -Uri $msys_mingw_get_url -OutFile C:\mingw-get.tar.xz
C:\7zip\7z e C:\mingw-get.tar.xz -o"C:\\"
C:\7zip\7z x C:\mingw-get.tar -o"C:\\MinGW"
Remove-Item C:\mingw-get.tar.xz -Force
Remove-Item C:\mingw-get.tar -Force

Write-Host "Installing MSYS for Cerbero into C:/MinGW using mingw-get"
Start-Process C:\MinGW\bin\mingw-get.exe -ArgumentList 'install msys-base mingw32-base mingw-developer-toolkit' -Wait

Write-Host "Installing MSYS2 into C:/msys64"
Invoke-WebRequest -Uri $msys2_url -OutFile C:\msys2-x86_64.tar.xz
C:\7zip\7z e C:\msys2-x86_64.tar.xz -o"C:\\"
C:\7zip\7z x C:\msys2-x86_64.tar -o"C:\\"
Remove-Item C:\msys2-x86_64.tar.xz -Force
Remove-Item C:\msys2-x86_64.tar -Force
Remove-Item C:\7zip -Recurse -Force

Write-Host "Installing Choco"
iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
refreshenv

Write-Host "Installing git-lfs"
choco install -y git-lfs
refreshenv

Write-Host "Installing Meson"
pip install meson

Write-Host "Complete"
