#!/bin/bash

# update all known gstreamer modules
# build them one by one
# report failures at the end
# run this from a directory that contains the checkouts for each of the
# modules

PIDS=

CORE="\
    gstreamer gst-plugins-base"
MODULES="\
    gst-plugins-good gst-plugins-ugly gst-plugins-bad \
    gst-ffmpeg \
    gst-python \
    gnonlin"

tmp=${TMPDIR-/tmp}
tmp=$tmp/git-update.$(date +%Y%m%d-%H%M-).$RANDOM.$RANDOM.$RANDOM.$$

(umask 077 && mkdir "$tmp") || {
  echo "Could not create temporary directory! Exiting." 1>&2
  exit 1
}

ERROR_LOG="$tmp/failures.log"
touch $ERROR_LOG
ERROR_RETURN=255

for m in $CORE $MODULES; do
  if test -d $m; then
    echo "+ updating $m"
    cd $m

    git pull origin master
    if test $? -ne 0
    then
      echo "$m: update (trying stash, pull, stash apply)" >> $ERROR_LOG
      git stash
      git pull origin master
      if test $? -ne 0
      then 
        echo "$m: update" >> $ERROR_LOG
        cd ..
        continue
      fi
      git stash apply
    fi

    git submodule update
    if test $? -ne 0
    then
      echo "$m: update (submodule)" >> $ERROR_LOG
      cd ..
      continue
    fi
    cd ..
  fi
done

build()
{
  if test -d $1; then
    cd $1
    if test ! -e Makefile -a -e autoregen.sh
    then
      echo "+ $1: autoregen.sh"
      ./autoregen.sh > "$tmp/$1-regen.log" 2>&1
      if test $? -ne 0
      then
        echo "$1: autoregen.sh [$tmp/$1-regen.log]" >> $ERROR_LOG
        cd ..
        return $ERROR_RETURN
      fi
      echo "+ $1: autoregen.sh done"
    fi
    else if test ! -e Makefile
    then
       echo "+ $1: autogen.sh"
      ./autogen.sh > "$tmp/$1-gen.log" 2>&1
      if test $? -ne 0
      then
        echo "$1: autogen.sh [$tmp/$1-gen.log]" >> $ERROR_LOG
        cd ..
        return $ERROR_RETURN
      fi
      echo "+ $1: autogen.sh done"
    fi

    echo "+ $1: make"
    make > "$tmp/$1-make.log" 2>&1
    if test $? -ne 0
    then
      echo "$1: make [$tmp/$1-make.log]" >> $ERROR_LOG
      cd ..
      return $ERROR_RETURN
    fi
    echo "+ $1: make done"

    if test "x$CHECK" != "x"; then
      echo "+ $1: make check"
      make check > "$tmp/$1-check.log" 2>&1
      if test $? -ne 0
      then
        echo "$1: check [$tmp/$1-check.log]" >> $ERROR_LOG
        cd ..
        return
      fi
      echo "+ $1: make check done"
    fi
    cd ..
  fi
}

beach()
{
if test -e $ERROR_LOG;  then
  echo "Failures:"
  echo
  cat $ERROR_LOG
else
  rm -rf "$tmp"
fi
exit
}

# build core and base plugins sequentially
# exit if build fails (excluding checks)
for m in $CORE; do
  build $m
  if [ $? -eq $ERROR_RETURN ]; then
  beach
  fi
done

# build other modules in parallel
for m in $MODULES; do
  build $m &
  PIDS="$PIDS $!"
done
wait $PIDS

beach

