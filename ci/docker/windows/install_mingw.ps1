[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

$msys_mingw_get_url = 'https://dotsrc.dl.osdn.net/osdn/mingw/68260/mingw-get-0.6.3-mingw32-pre-20170905-1-bin.tar.xz'

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

Write-Host "MSYS/MinGW Install Complete"
