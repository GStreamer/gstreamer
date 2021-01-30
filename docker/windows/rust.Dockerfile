# escape=`

# Expect this to be set when calling docker build with
# --build-arg BASE_IMAGE="" and make it fail if not set.
ARG BASE_IMAGE="inavlid.gstreamer.freedesktop.org/invalid"
FROM $BASE_IMAGE

ARG DEFAULT_BRANCH="master"
ARG RUST_VERSION="1.52.1"

COPY install_gst.ps1 C:\
RUN C:\install_gst.ps1
RUN choco install -y pkgconfiglite
ENV PKG_CONFIG_PATH="C:/lib/pkgconfig"

ADD https://win.rustup.rs/x86_64 C:\rustup-init.exe
RUN C:\rustup-init.exe -y --profile minimal --default-toolchain $env:RUST_VERSION

# Uncomment for easy testing
# RUN git clone --depth 1 https://gitlab.freedesktop.org/gstreamer/gstreamer-rs.git
# RUN cd gstreamer-rs; cmd.exe /C "C:\BuildTools\Common7\Tools\VsDevCmd.bat -host_arch=amd64 -arch=amd64; cargo build --all; cargo test --all"