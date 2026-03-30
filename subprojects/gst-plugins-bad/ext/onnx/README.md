ONNX Build Instructions


### Build

 1. do a recursive checkout of [onnxruntime tag 1.16.3](https://github.com/microsoft/onnxruntime)
 2. `$SRC_DIR` and `$BUILD_DIR` are local source and build directories
 3. To run with CUDA, both [CUDA](https://developer.nvidia.com/cuda-downloads) and [cuDNN](https://docs.nvidia.com/deeplearning/cudnn/archives/cudnn_762/cudnn-install/index.html) libraries must be installed.

```
$ cd $SRC_DIR
$ git clone --recursive https://github.com/microsoft/onnxruntime.git && cd onnxruntime && git checkout -b v1.16.3 refs/tags/v1.16.3
$ mkdir $BUILD_DIR/onnxruntime && cd $BUILD_DIR/onnxruntime
$ apt-get update && apt-get install -y libeigen3-dev
```

1. CPU
```
$ cmake -Donnxruntime_BUILD_SHARED_LIB=ON -DBUILD_TESTING=OFF -Donnxruntime_BUILD_UNIT_TESTS=OFF -Donnxruntime_USE_PREINSTALLED_EIGEN=ON -Deigen_SOURCE_PATH=/usr/include/eigen3 $SRC_DIR/onnxruntime/cmake && make -j$(nproc) && sudo make install
```
2. CUDA
```
cmake -Donnxruntime_BUILD_SHARED_LIB=ON -DBUILD_TESTING=OFF -Donnxruntime_BUILD_UNIT_TESTS=OFF -Donnxruntime_USE_CUDA=ON -Donnxruntime_CUDA_HOME=/usr/local/cuda -Donnxruntime_CUDNN_HOME=/usr/local/cuda -DCMAKE_CUDA_ARCHITECTURES=native -DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc -Donnxruntime_USE_PREINSTALLED_EIGEN=ON -Deigen_SOURCE_PATH=/usr/include/eigen3 $SRC_DIR/onnxruntime/cmake && make -j$(nproc) && sudo make install
```
3. Intel oneDNN

3.0 install [intel oneDNN](https://www.intel.com/content/www/us/en/developer/tools/oneapi/onednn.html)

3.1 clone, build and install [Khronos OpenCL SDK](https://github.com/KhronosGroup/OpenCL-SDK.git). Build dependencies for Fedora are

`sudo dnf install libudev-devel libXrandr-devel  mesa-libGLU-devel mesa-libGL-devel    libX11-devel intel-opencl`

3.2 build and install `onnxruntime` :
```
cmake -Donnxruntime_BUILD_SHARED_LIB=ON -DBUILD_TESTING=OFF -Donnxruntime_BUILD_UNIT_TESTS=OFF -Donnxruntime_USE_DNNL=ON -Donnxruntime_DNNL_GPU_RUNTIME=ocl -Donnxruntime_DNNL_OPENCL_ROOT=$SRC_DIR/OpenCL-SDK/install  $SRC_DIR/onnxruntime/cmake && make -j$(nproc) && sudo make install
```

4. ROCm (AMD GPU)

On Fedora 45+, the ROCm execution provider is available as a pre-built package:

```
sudo dnf install onnxruntime-rocm-devel
```

The ROCm onnxruntime libraries are installed to `/usr/lib64/rocm/lib/`,

```
PKG_CONFIG_PATH=/usr/lib64/rocm/lib/pkgconfig:$PKG_CONFIG_PATH \
LD_LIBRARY_PATH=/usr/lib64/rocm/lib:$LD_LIBRARY_PATH \
meson setup <builddir>
```

Missing abseil linkage (onnxruntime-rocm <= 1.22.2):
`libonnxruntime_providers_rocm.so` uses abseil symbols but does not declare the dependency on
`libabsl_raw_hash_set`. This causes a runtime error when loading the ROCm provider:

Temporary hack:
Meson sets `LD_PRELOAD` to work around this when the ROCm
onnxruntime is detected. No manual action is required when building with meson devenv.

Selecting the GPU at runtime:

If the system has multiple GPUs (e.g. a discrete AMD GPU and an integrated Vega), set
`HIP_VISIBLE_DEVICES` to select the correct device before running the pipeline:

```
export HIP_VISIBLE_DEVICES=0   # 0 = first GPU reported by rocm-smi
```

Without this, ROCm may pick the wrong device. Verify GPU usage during inference with:

```
rocm-smi --showpids
```

5. MIGraphX (AMD GPU)

MIGraphX EP is not available as a pre-built package and must be compiled from source.

5.1 Install MIGraphX and ROCm dependencies:

```
sudo dnf install migraphx-devel miopen-devel rocblas-devel patch
```

5.2 Clone and build onnxruntime with the MIGraphX EP:

```
git clone --recursive --branch v1.22.2 https://github.com/microsoft/onnxruntime.git
cd onnxruntime
./build.sh \
    --config Release \
    --parallel \
    --build_shared_lib \
    --use_migraphx \
    --migraphx_home /usr \
    --skip_submodule_sync \
    --compile_no_warning_as_error \
    --allow_running_as_root \
    --cmake_extra_defines \
        CMAKE_INSTALL_PREFIX=/usr/local \
        CMAKE_INSTALL_LIBDIR=lib64
cmake --install build/Linux/Release --prefix /usr/local
sudo ldconfig
```

Note: `--compile_no_warning_as_error` is required on GCC 16+ due to stricter check
`--migraphx_home /usr` matches the Fedora RPM install layout for MIGraphX.

5.3 Configure GStreamer pointing to the onnxruntime install:

```
PKG_CONFIG_PATH=/usr/local/lib64/pkgconfig:$PKG_CONFIG_PATH \
LD_LIBRARY_PATH=/usr/local/lib64:$LD_LIBRARY_PATH \
meson setup <builddir>
```

When onnxruntime is detected in `/usr/local`, meson automatically sets `PKG_CONFIG_PATH`,
`LD_LIBRARY_PATH`, and `HIP_VISIBLE_DEVICES=0` in the devenv. No manual action is required
when building with meson devenv.
