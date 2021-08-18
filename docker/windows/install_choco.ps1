Get-Date
Write-Host "Installing Chocolatey"
Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
Import-Module "$env:ProgramData\chocolatey\helpers\chocolateyProfile.psm1"
Update-SessionEnvironment

choco install -y vcredist140
$vc140_install = $?

Write-Host "Installing Chocolatey packages"
choco install -y cmake --installargs 'ADD_CMAKE_TO_PATH=System'
$cmake_install = $?

choco install -y git --params "/NoAutoCrlf /NoCredentialManager /NoShellHereIntegration /NoGuiHereIntegration /NoShellIntegration"
$git_install = $?

choco install -y python3 git-lfs 7zip
$rest_installs = $?

if (!($vc140_install -and $cmake_install -and $git_install -and $rest_installs)) {
    Write-Host "Failed to install some dependencies from choco"
    Exit 1
}
