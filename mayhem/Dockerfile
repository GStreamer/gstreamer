FROM ubuntu AS builder

RUN apt update -y && DEBIAN_FRONTEND=noninteractive apt install -y \
  python3 \
  python3-pip \
  afl++ \
  ninja-build \
  git \
  flex \
  bison \
  cmake \
  pkg-config \
  libglib2.0-dev \
  libjpeg-dev \
  libunwind-dev \
  libdw-dev
RUN pip3 install meson

WORKDIR /work
COPY . .

RUN CC=afl-gcc CXX=afl-g++ meson setup \
  --libdir=lib \
  --default-library=shared \
  #--force-fallback-for=zlib \
  -Dharnesses=enabled \
  -Dauto_features=disabled \
  -Db_lundef=false \
  -Dglib:libmount=disabled \
  -Dglib:tests=false \
  -Ddoc=disabled \
  -Dexamples=disabled \
  -Dintrospection=disabled \
  -Dgood=enabled \
  -Dugly=disabled \
  -Dbad=disabled \
  -Dlibav=disabled \
  #-Dges=disabled \
  -Dvaapi=disabled \
  -Dsharp=disabled \
  -Drs=disabled \
  -Dpython=disabled \
  -Dlibnice=disabled \
  #-Dtls=disabled \
  -Ddevtools=disabled \
  -Dtools=disabled \
  #-Drtsp_server=disabled \
  -Dgst-examples=disabled \
  -Dqt5=disabled \
  -Dqt6=disabled \
  -Dorc=disabled \
  #-Dnls=disabled \
  -Dwebrtc=disabled \
  -Dgtk_doc=disabled \
  -Dgstreamer:tracer_hooks=false \
  -Dgst-plugins-base:opus=disabled \
  -Dgst-plugins-base:pango=disabled \
  -Dgst-plugins-good:jpeg=enabled \
  -Dgstreamer:check=enabled \
  builddir \
    .
RUN CC=afl-gcc CXX=afl-g++ ninja -C builddir

FROM ubuntu
RUN apt update -y && DEBIAN_FRONTEND=noninteractive apt install -y \
  libasan6 \
  gstreamer1.0-x \
  gstreamer1.0-tools \
  gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-ugly
COPY --from=builder /work/builddir/harnesses /work/builddir/harnesses
COPY --from=builder /work/harnesses /work/harnesses
