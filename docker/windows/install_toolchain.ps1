[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

$msvc_2017_url = 'https://aka.ms/vs/15/release/vs_buildtools.exe'
$msys2_url = 'https://github.com/msys2/msys2-installer/releases/download/2021-02-15/msys2-base-x86_64-20210215.tar.xz'
$msys_mingw_get_url = 'https://dotsrc.dl.osdn.net/osdn/mingw/68260/mingw-get-0.6.3-mingw32-pre-20170905-1-bin.tar.xz'

Get-Date
Write-Host "Downloading Visual Studio 2017 build tools"
Invoke-WebRequest -Uri $msvc_2017_url -OutFile C:\vs_buildtools.exe

Get-Date
Write-Host "Installing Visual Studio 2017"
Start-Process -NoNewWindow -Wait C:\vs_buildtools.exe -ArgumentList '--wait --quiet --norestart --nocache --installPath C:\BuildTools --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
if (!$?) {
  Write-Host "Failed to install Visual Studio tools"
  Exit 1
}
Remove-Item C:\vs_buildtools.exe -Force

Get-Date
Write-Host "Downloading and extracting mingw-get for MSYS"
Invoke-WebRequest -Uri $msys_mingw_get_url -OutFile C:\mingw-get.tar.xz
7z e C:\mingw-get.tar.xz -o"C:\\"
$res1 = $?
7z x C:\mingw-get.tar -o"C:\\MinGW"
$res2 = $?

if (!($res1 -and $res2)) {
  Write-Host "Failed to extract mingw-get"
  Exit 1
}

Remove-Item C:\mingw-get.tar.xz -Force
Remove-Item C:\mingw-get.tar -Force

Get-Date
Write-Host "Installing MSYS for Cerbero into C:/MinGW using mingw-get"
Start-Process -Wait C:\MinGW\bin\mingw-get.exe -ArgumentList 'install msys-base mingw32-base mingw-developer-toolkit'
if (!$?) {
  Write-Host "Failed to install Msys for cerbero using MinGW"
  Exit 1
}

Get-Date
Write-Host "Installing MSYS2 into C:/msys64"
Invoke-WebRequest -Uri $msys2_url -OutFile C:\msys2-x86_64.tar.xz

7z e C:\msys2-x86_64.tar.xz -o"C:\\"
$res1 = $?
7z x C:\msys2-x86_64.tar -o"C:\\"
$res2 = $?

if (!($res1 -and $res2)) {
  Write-Host "Failed to extract msys2"
  Exit 1
}

Remove-Item C:\msys2-x86_64.tar.xz -Force
Remove-Item C:\msys2-x86_64.tar -Force

Get-Date
Write-Host "Installing Meson"
pip3 install meson
if (!$?) {
  Write-Host "Failed to install meson from pip"
  Exit 1
}

Write-Host "Toolchain Install Complete"
