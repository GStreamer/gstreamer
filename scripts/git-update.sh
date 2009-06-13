#!/bin/bash

# update all known gstreamer modules
# build them one by one
# report failures at the end
# run this from a directory that contains the checkouts for each of the
# modules

FAILURE=

MODULES="\
    gstreamer gst-plugins-base \
    gst-plugins-good gst-plugins-ugly gst-plugins-bad \
    gst-ffmpeg \
    gst-python \
    gnonlin"

for m in $MODULES; do
  if test -d $m; then
    echo "+ updating $m"
    cd $m

    git pull origin master
    if test $? -ne 0
    then
      git stash
      git pull origin master
      if test $? -ne 0
      then 
        git stash apply
        FAILURE="$FAILURE$m: update\n"
      else
        git stash apply
      fi
      cd ..
      continue
    fi
    git submodule update
    if test $? -ne 0
    then
      FAILURE="$FAILURE$m: update\n"
      cd ..
      continue
    fi
    cd ..
  fi
done

# then build
for m in $MODULES; do
  if test -d $m; then
    cd $m
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
