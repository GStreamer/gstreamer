[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

# FIXME: Python fails to validate github.com SSL certificate, unless we first
# run a dummy download to force refreshing Windows' CA database.
# See: https://bugs.python.org/issue36137
(New-Object System.Net.WebClient).DownloadString("https://github.com") >$null

Write-Host "Cloning GStreamer"
git clone -b $env:DEFAULT_BRANCH https://gitlab.freedesktop.org/gstreamer/gstreamer.git C:\gstreamer

# download the subprojects to try and cache them
Write-Host "Downloading subprojects"
meson subprojects download --sourcedir C:\gstreamer

Write-Host "Caching subprojects into /subprojects/"
python C:\gstreamer/ci/scripts/handle-subprojects-cache.py --build C:\gstreamer/subprojects/
Remove-Item -Recurse -Force C:\gstreamer
