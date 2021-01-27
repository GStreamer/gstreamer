$env:DEFAULT_BRANCH='master'
$env:VERSION='v18'
$env:tag ="registry.freedesktop.org/gstreamer/gst-ci/amd64/windows:$env:VERSION-$env:DEFAULT_BRANCH"

Get-Date
Write-Output "Building $env:tag"
docker build --build-arg DEFAULT_BRANCH=$env:DEFAULT_BRANCH -f Dockerfile -t $env:tag .
if (!$?) {
  Write-Host "Failed to build docker image $env:tag"
  Exit 1
}

# Get-Date
# Write-Output "Pushing $env:tag"
# docker push $env:tag
# if (!$?) {
#   Write-Host "Failed to push docker image $env:tag"
#   Exit 1
# }

Get-Date
Write-Output "Build Finished"