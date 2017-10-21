#!/bin/sh
echo Building gstreamer/base-fedora:latest

docker build -t gstreamer/base-fedora:latest . -f Dockerfile-fedora

echo Building gstreamer/base-ubuntu:latest

docker build -t gstreamer/base-ubuntu:latest . -f Dockerfile-ubuntu

