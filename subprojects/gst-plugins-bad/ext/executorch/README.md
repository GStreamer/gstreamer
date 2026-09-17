## Executorch Build Instructions

### Build

1. Do a checkout of [executorch v1.3.1](https://github.com/pytorch/executorch)
2. `$SRC_DIR` and `$BUILD_DIR` are local source and build directories
```bash
 $ cd $SRC_DIR
 $ git clone --branch v1.3.1 https://github.com/pytorch/executorch.git
 $ cd executorch
 $ git submodule update --init --recursive
```

3. Setup Executorch
```
 $ python -m venv executorch_venv
 $ source executorch_venv/bin/activate
 $ pip install -r ./.ci/docker/requirements-ci.txt
 $ ./install_executorch.sh
```

4. Build the executorch runtime components (includes XNNPACK CPU backend)
```bash
 $ mkdir $BUILD_DIR/executorch && cd $BUILD_DIR/executorch
 $ cmake $SRC_DIR/executorch \
   -DCMAKE_BUILD_TYPE=Release \
   -DBUILD_SHARED_LIBS=ON \
   -DEXECUTORCH_BUILD_EXTENSION_TENSOR=ON \
   -DEXECUTORCH_BUILD_EXTENSION_DATA_LOADER=ON \
   -DEXECUTORCH_BUILD_EXTENSION_MODULE=ON \
   -DEXECUTORCH_BUILD_EXTENSION_RUNNER_UTIL=ON \
   -DEXECUTORCH_BUILD_EXECUTOR_RUNNER=OFF \
   -DEXECUTORCH_BUILD_EXTENSION_FLAT_TENSOR=ON \
   -DEXECUTORCH_BUILD_XNNPACK=ON \
   -DEXECUTORCH_BUILD_EXTENSION_NAMED_DATA_MAP=ON \
   -DCMAKE_INSTALL_PREFIX=/usr/local
 $ make -j9
 $ sudo make install
```

5. Build gstreamer with executorch build dir
```bash
 $ cd gstreamer
 $ meson setup builddir -Dgst-plugins-bad:executorch=enabled
 $ ninja -C builddir
```
