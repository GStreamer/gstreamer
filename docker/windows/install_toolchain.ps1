[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

$msvc_2017_url = 'https://aka.ms/vs/15/release/vs_buildtools.exe'
$msys2_url = 'https://github.com/msys2/msys2-installer/releases/download/2020-05-22/msys2-base-x86_64-20200522.tar.xz'
$msys_mingw_get_url = 'https://dotsrc.dl.osdn.net/osdn/mingw/68260/mingw-get-0.6.3-mingw32-pre-20170905-1-bin.tar.xz'

Write-Host "Installing VisualStudio"
Invoke-WebRequest -Uri $msvc_2017_url -OutFile C:\vs_buildtools.exe
Start-Process C:\vs_buildtools.exe -ArgumentList '--quiet --wait --norestart --nocache --installPath C:\BuildTools --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' -Wait
Remove-Item C:\vs_buildtools.exe -Force

Write-Host "Downloading and extracting mingw-get for MSYS"
Invoke-WebRequest -Uri $msys_mingw_get_url -OutFile C:\mingw-get.tar.xz
7z e C:\mingw-get.tar.xz -o"C:\\"
7z x C:\mingw-get.tar -o"C:\\MinGW"
Remove-Item C:\mingw-get.tar.xz -Force
Remove-Item C:\mingw-get.tar -Force

Write-Host "Installing MSYS for Cerbero into C:/MinGW using mingw-get"
Start-Process C:\MinGW\bin\mingw-get.exe -ArgumentList 'install msys-base mingw32-base mingw-developer-toolkit' -Wait

Write-Host "Installing MSYS2 into C:/msys64"
Invoke-WebRequest -Uri $msys2_url -OutFile C:\msys2-x86_64.tar.xz
7z e C:\msys2-x86_64.tar.xz -o"C:\\"
7z x C:\msys2-x86_64.tar -o"C:\\"
Remove-Item C:\msys2-x86_64.tar.xz -Force
Remove-Item C:\msys2-x86_64.tar -Force

Write-Host "Installing Meson"
pip install meson

Write-Host "Complete"
