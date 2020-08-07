$env:DEFAULT_BRANCH='master'
$env:VERSION='v12'
$env:tag ="registry.freedesktop.org/gstreamer/gst-ci/amd64/windows:$env:VERSION-$env:DEFAULT_BRANCH"
echo "Building $env:tag"
docker build --build-arg DEFAULT_BRANCH=$env:DEFAULT_BRANCH -f Dockerfile -t $env:tag .
# docker push $env:tag
