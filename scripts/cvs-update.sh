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
  ; do
  if test -d $m; then
    cd $m
    cvs update -dP
    if test $? -ne 0
    then
      FAILURE="$FAILURE$m: update\n"
      cd ..
      continue
    fi
    if test ! -e Makefile
    then
      ./autoregen.sh
      if test $? -ne 0
      then
        FAILURE="$FAILURE$m: autoregen.sh\n"
        cd ..
        continue
      fi
    fi

    make $@
    if test $? -ne 0
    then
      FAILURE="$FAILURE$m: make\n"
      cd ..
      continue
    fi

    make $@ check
    if test $? -ne 0
    then
      FAILURE="$FAILURE$m: check\n"
      cd ..
      continue
    fi
    cd ..
  fi
done

if test "x$FAILURE" != "x";  then
  echo "Failures:"
  echo
  echo -e $FAILURE
fi
