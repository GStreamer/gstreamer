#!/usr/bin/env bash

export PROJECT_ROOT_PATH=`pwd`
#export PROJECT_BUILD_TYPE=debug
export PROJECT_BUILD_TYPE=release

function build_init()
{
if [ "x${PROJECT_ROOT_PATH}" == "x" ]; then
    echo "pwd is `pwd`"
    exit 1
fi
}

function sanity_check_for_ubuntu
{
    sudo apt-get remove libpixman-1-dev
}

function build_gstreamer()
{
meson setup --buildtype ${PROJECT_BUILD_TYPE} --default-library=static -Dgpl=enabled -Dgst-full-target-type=static_library -Dgstreamer:tools=enabled -Dgst-plugins-good:lame=disabled  --prefix=${PROJECT_ROOT_PATH}/out/x86-linux/prefix ${PROJECT_ROOT_PATH}/out/x86-linux

#meson compile -C ${PROJECT_ROOT_PATH}/out/x86-linux --verbose
meson compile -C ${PROJECT_ROOT_PATH}/out/x86-linux
meson install -C ${PROJECT_ROOT_PATH}/out/x86-linux
}


build_init

#sanity_check_for_ubuntu

build_gstreamer
