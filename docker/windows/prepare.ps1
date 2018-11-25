[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

Write-Host "Installing VisualStudio"
Invoke-WebRequest -Uri 'https://aka.ms/vs/15/release/vs_buildtools.exe' -OutFile C:\vs_buildtools.exe
Start-Process C:\vs_buildtools.exe -ArgumentList '--quiet --wait --norestart --nocache --installPath C:\BuildTools --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' -Wait
Remove-Item C:\vs_buildtools.exe -Force

Write-Host "Installing Python"
Invoke-WebRequest -Uri 'https://www.python.org/ftp/python/3.7.0/python-3.7.0.exe' -OutFile C:\python-3.7.0.exe
Start-Process C:\python-3.7.0.exe -ArgumentList '/quiet InstallAllUsers=1 PrependPath=1' -Wait
Remove-Item C:\python-3.7.0.exe -Force

Write-Host "Installing Git"
Invoke-WebRequest -Uri 'https://github.com/git-for-windows/git/releases/download/v2.19.1.windows.1/MinGit-2.19.1-64-bit.zip' -OutFile C:\mingit.zip
Expand-Archive C:\mingit.zip -DestinationPath c:\mingit
Remove-Item C:\mingit.zip -Force
$env:PATH = [System.Environment]::GetEnvironmentVariable('PATH', 'Machine') + ';' + 'c:\mingit\cmd'
[Environment]::SetEnvironmentVariable('PATH', $env:PATH, [EnvironmentVariableTarget]::Machine)

Write-Host "Installing 7zip"
Invoke-WebRequest -Uri 'https://www.7-zip.org/a/7z1805-x64.exe' -OutFile C:\7z-x64.exe
Start-Process C:\7z-x64.exe -ArgumentList '/S /D=C:\7zip\' -Wait
Remove-Item C:\7z-x64.exe -Force

Write-Host "Installing MSYS2"
Invoke-WebRequest -Uri 'https://ayera.dl.sourceforge.net/project/msys2/Base/x86_64/msys2-base-x86_64-20180531.tar.xz' -OutFile C:\msys2-x86_64.tar.xz
C:\7zip\7z e C:\msys2-x86_64.tar.xz -Wait
C:\7zip\7z x C:\msys2-x86_64.tar -o"C:\\"
Remove-Item C:\msys2-x86_64.tar.xz -Force
Remove-Item C:\msys2-x86_64.tar -Force
Remove-Item C:\7zip -Recurse -Force

# FIXME: This works but then docker fails to save the image. Needs to investigate why.
#$env:PATH += ";C:\msys64\usr\bin;C:\msys64\mingw64/bin;C:\msys64\mingw32/bin"
#C:\msys64\usr\bin\bash -c "pacman-key --init && pacman-key --populate msys2 && pacman-key --refresh-keys"
#C:\msys64\usr\bin\bash -c "pacman -Syuu --noconfirm"
#C:\msys64\usr\bin\bash -c "pacman -Sy --noconfirm --needed mingw-w64-x86_64-toolchain ninja"

pip install meson

git config --global user.email "gst-build@gstreamer.net"
git config --global user.name "Gstbuild Runner"

# Download gst-build and all its subprojects
git clone https://gitlab.freedesktop.org/gstreamer/gst-build.git C:\gst-build
meson subprojects download --sourcedir C:\gst-build
