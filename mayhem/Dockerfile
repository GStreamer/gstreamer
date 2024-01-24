FROM ubuntu AS builder

RUN apt update -y && apt install -y python3 python3-pip build-essential clang ninja-build git flex bison cmake pkg-config
RUN pip3 install meson

WORKDIR /work
COPY . .

RUN CC=clang CXX=clang++ meson \
    --libdir=lib \
    --default-library=shared \
    --force-fallback-for=zlib \
    -Dauto_features=disabled \
    -Db_lundef=false \
    -Dharnesses=enabled \
    -Dglib:libmount=disabled \
    -Dglib:tests=false \
    -Ddoc=disabled \
    -Dexamples=disabled \
    -Dintrospection=disabled \
    -Dgood=enabled \
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
    -Dorc=disabled \
    -Dgtk_doc=disabled \
    -Dgstreamer:tracer_hooks=false \
    -Dgst-plugins-base:opus=disabled \
    -Dgst-plugins-base:pango=disabled \
    -Dgst-plugins-good:jpeg=enabled \
    builddir \
    .
RUN CC=clang CXX=clang++ ninja -C builddir

FROM ubuntu
COPY --from=builder /work/builddir/ /
