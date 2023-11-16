[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

$rust_version = '1.74.0'
$rustup_url = 'https://win.rustup.rs/x86_64'

Invoke-WebRequest -Uri $rustup_url -OutFile C:\rustup-init.exe

if (!$?) {
  Write-Host "Failed to download rustup"
  Exit 1
}

C:\rustup-init.exe -y --profile minimal --default-toolchain $rust_version

if (!$?) {
  Write-Host "Failed to install rust"
  Exit 1
}
