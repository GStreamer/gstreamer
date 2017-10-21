#!/bin/sh
echo Building gstreamer/build-base-fedora:latest

docker build -t gstreamer/build-base-fedora:latest . -f Dockerfile-fedora

echo Building gstreamer/build-base-ubuntu:latest

docker build -t gstreamer/build-base-ubuntu:latest . -f Dockerfile-ubuntu
