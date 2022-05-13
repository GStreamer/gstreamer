$env:ErrorActionPreference='Stop'

$env:DEFAULT_BRANCH='main'
$env:VERSION='test'
$env:tag ="registry.freedesktop.org/gstreamer/gst-ci/amd64/windows:$env:VERSION-$env:DEFAULT_BRANCH"

Set-Location './docker/windows/'

Get-Date
Write-Output "Building $env:tag"
docker build --isolation=hyperv -m 12g --build-arg DEFAULT_BRANCH=$env:DEFAULT_BRANCH -f Dockerfile -t $env:tag .
if (!$?) {
  Write-Host "Failed to build docker image $env:tag"
  Exit 1
}

Get-Date
Write-Output "Build Finished"