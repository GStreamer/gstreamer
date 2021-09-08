$env:ErrorActionPreference='Stop'

$env:DEFAULT_BRANCH='master'
$env:VERSION='v18'
$env:tag ="registry.freedesktop.org/gstreamer/gst-ci/amd64/windows:$env:VERSION-$env:DEFAULT_BRANCH"
$env:rust_tag ="registry.freedesktop.org/gstreamer/gst-ci/amd64/windows-rust:$env:VERSION-$env:DEFAULT_BRANCH"

Set-Location './docker/windows/'

Get-Date
Write-Output "Building $env:tag"
docker build --isolation=hyperv -m 12g --build-arg DEFAULT_BRANCH=$env:DEFAULT_BRANCH -f Dockerfile -t $env:tag .
if (!$?) {
  Write-Host "Failed to build docker image $env:tag"
  Exit 1
}

Get-Date
Write-Output "Building $env:rust_tag"
docker build --isolation=hyperv -m 12g --build-arg DEFAULT_BRANCH=$env:DEFAULT_BRANCH -f rust.Dockerfile -t $env:rust_tag .
if (!$?) {
  Write-Host "Failed to build docker image $env:rust_tag"
  Exit 1
}

# Get-Date
# Write-Output "Pushing $env:tag"
# docker push $env:tag
# if (!$?) {
#   Write-Host "Failed to push docker image $env:tag"
#   Exit 1
# }

# Get-Date
# Write-Output "Pushing $env:rust_tag"
# docker push $env:rust_tag
# if (!$?) {
#   Write-Host "Failed to push docker image $env:rust_tag"
#   Exit 1
# }


Get-Date
Write-Output "Build Finished"