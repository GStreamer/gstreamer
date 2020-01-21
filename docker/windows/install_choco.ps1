Write-Host "Installing Choco"
Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
Write-Host "Installing Choco packages"
choco install -y cmake --installargs 'ADD_CMAKE_TO_PATH=System'
choco install -y python3 git git-lfs 7zip