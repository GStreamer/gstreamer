FROM ubuntu AS builder

RUN apt update -y && apt install -y python3 python3-pip build-essential clang ninja-build git flex bison cmake pkg-config
RUN pip3 install meson

WORKDIR /work
COPY . .
#RUN meson setup builddir
#RUN meson compile -C builddir
#RUN meson subprojects update

RUN CC=clang CXX=clang++ meson \
    --libdir=lib \
    --default-library=shared \
    --force-fallback-for=zlib \
    -Db_lundef=false \
    -Dharnesses=enabled \
    -Dglib:libmount=disabled \
    -Dglib:tests=false \
    -Ddoc=disabled \
    -Dexamples=disabled \
    -Dintrospection=disabled \
    -Dgood=disabled \
    -Dugly=disabled \
    -Dbad=disabled \
    -Dlibav=disabled \
    -Dges=disabled \
    -Dvaapi=disabled \
    -Dsharp=disabled \
    -Drs=disabled \
    -Dpython=disabled \
    -Dlibnice=disabled \
    -Ddevtools=disabled \
    -Drtsp_server=disabled \
    -Dgst-examples=disabled \
    -Dqt5=disabled \
    -Dorc=enabled \
    -Dgtk_doc=disabled \
    -Dgstreamer:tracer_hooks=false \
    -Dgst-plugins-base:opus=disabled \
    -Dgst-plugins-base:pango=disabled \
    builddir \
    .
RUN CC=clang CXX=clang++ ninja -C builddir
#RUN ninja -C builddir install
