# Install the Google Cloud Storage dependencies.

```
sudo apt-get install \
    cmake \
    libcurl3-gnutls-dev \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler-grpc \
    nlohmann-json3-dev
```

# Build the Google Cloud Storage library

```
git clone https://github.com/google/crc32c.git
cd crc32c && git checkout -b 1.1.1
mkdir build && cd build
cmake .. \
    -GNinja \
    -DCMAKE_INSTALL_PREFIX:PATH=~/dev/gstreamer/prefix \
    -DCMAKE_INSTALL_LIBDIR:PATH=lib \
    -DBUILD_SHARED_LIBS=YES \
    -DCRC32C_USE_GLOG=NO \
    -DCRC32C_BUILD_TESTS=NO \
    -DCRC32C_BUILD_BENCHMARKS=NO
ninja && ninja install
cd ../..

git clone https://github.com/abseil/abseil-cpp.git
cd abseil-cpp && git checkout master
mkdir build && cd build
cmake .. \
    -GNinja \
    -DBUILD_TESTING=NO \
    -DCMAKE_INSTALL_PREFIX:PATH=~/dev/gstreamer/prefix \
    -DCMAKE_INSTALL_LIBDIR:PATH=lib \
    -DBUILD_SHARED_LIBS=YES
ninja && ninja install
cd ../..

git clone https://github.com/googleapis/google-cloud-cpp.git
cd  google-cloud-cpp && git checkout -b v1.31.1
mkdir build && cd build
cmake .. \
    -GNinja \
    -DCMAKE_INSTALL_PREFIX:PATH=~/dev/gstreamer/prefix \
    -DCMAKE_INSTALL_LIBDIR:PATH=lib \
    -DBUILD_SHARED_LIBS=YES \
    -DBUILD_TESTING=NO \
    -DGOOGLE_CLOUD_CPP_ENABLE=storage
ninja && ninja install
cd ../..
```

# Running the gs elements locally

When running from the command line or in a container running locally, simply
set the credentials by exporting GOOGLE_APPLICATION_CREDENTIALS. If you are
not familiar with this environment variable, check the documentation
https://cloud.google.com/docs/authentication/getting-started
Note that you can restrict a service account to the role Storage Admin or
Storage Object Creator instead of the Project Owner role from the above
documentation.

# Running the gs elements in Google Cloud Run

Add the Storage Object Viewer role to the service account assigned to the
Cloud Run service where gssrc runs. For gssink add the role Storage Object
Creator. Then just set the service-account-email property on the element.

# Running the gs elements in Google Cloud Kubernetes

You need to set GOOGLE_APPLICATION_CREDENTIALS in the container and ship the
json file to which the environment variable points to.
