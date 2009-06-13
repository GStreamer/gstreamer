#!/bin/bash

# update all known gstreamer modules
# build them one by one
# report failures at the end
# run this from a directory that contains the checkouts for each of the
# modules

FAILURE=

for m in \
  gstreamer gst-plugins-base \
  gst-plugins-good gst-plugins-ugly gst-plugins-bad \
  gst-ffmpeg \
  gst-python \
  gnonlin \
  ; do
  if test -d $m; then
    cd $m
      echo $m
      git branch | grep '*'
      git log | head -n 1
    cd ..
  fi
done
