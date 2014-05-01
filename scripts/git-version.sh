#!/bin/bash

# display the latest commit in the current branch of all gstreamer modules.
# run this from a directory that contains the checkouts for each of the
# modules

for m in \
  gstreamer gst-plugins-base \
  gst-plugins-good gst-plugins-ugly gst-plugins-bad \
  gst-ffmpeg gst-libav \
  gst-editing-services \
  gst-python gstreamer-sharp \
  gnonlin \
  gst-rtsp-server \
  gst-omx \
  gst-devtools \
  ; do
  if test -d $m; then
    cd $m
      echo $m
      git branch | grep '*'
      git log | head -n 3 | sed -n '1p;3p'
      echo ""
    cd ..
  fi
done
