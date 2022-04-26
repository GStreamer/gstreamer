[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

# Download gstreamer and all its subprojects
git clone -b $env:DEFAULT_BRANCH --depth 1 https://gitlab.freedesktop.org/gstreamer/gstreamer.git C:\gstreamer
if (!$?) {
  Write-Host "Failed to clone gstreamer"
  Exit 1
}

Set-Location C:\gstreamer

# Copy the cache we already have in the image to avoid massive redownloads
Move-Item C:/subprojects/*  C:\gstreamer\subprojects

if (!$?) {
  Write-Host "Failed to copy subprojects cache"
  Exit 1
}

# Update the subprojects cache
Write-Output "Running meson subproject reset"
meson subprojects update --reset

if (!$?) {
  Write-Host "Failed to reset subprojects state"
  Exit 1
}

$env:MESON_ARGS = "-Dglib:installed_tests=false " +
    "-Dlibnice:tests=disabled " +
    "-Dlibnice:examples=disabled " +
    "-Dffmpeg:tests=disabled " +
    "-Dopenh264:tests=disabled " +
    "-Dpygobject:tests=false " +
    "-Dugly=enabled " +
    "-Dbad=enabled " +
    "-Dges=enabled " +
    "-Drtsp_server=enabled " +
    "-Ddevtools=enabled " +
    "-Dsharp=disabled " +
    "-Dpython=disabled " +
    "-Dlibav=disabled " +
    "-Dvaapi=disabled " +
    "-Dgst-plugins-base:pango=enabled " +
    "-Dgst-plugins-good:cairo=enabled " +
    "-Dgpl=enabled "

Write-Output "Building gst"
cmd.exe /C "C:\BuildTools\Common7\Tools\VsDevCmd.bat -host_arch=amd64 -arch=amd64 && meson _build $env:MESON_ARGS && meson compile -C _build && ninja -C _build install"

if (!$?) {
  Write-Host "Failed to build and install gst"
  Exit 1
}

git clean -fdxx

if (!$?) {
  Write-Host "Failed to git clean"
  Exit 1
}
