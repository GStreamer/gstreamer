#!/bin/bash

# automatic testing of some of the plugins using gstreamer-launch

MEDIA=/home/thomas/media
GSTL=gstreamer-launch

run_file_test()
# run a pipe between filesrc and fakesink to test a set of plugins
# first argument is the test name
# second argument is the filename to work on
# third argument is the part between filesrc and fakesink
{
  NAME=$1
  FILE=$2
  PIPE=$3
  
  echo -n "Testing $NAME ... "
  COMMAND="$GSTL filesrc location=$MEDIA/$FILE ! $PIPE ! fakesink silent=true"
  $COMMAND > /dev/null 2> /dev/null
  if test $?; then PASSED="yes"; else PASSED="no"; fi
  if test "x$PASSED"="xyes"; then echo "passed."; else echo "failed"; fi 
}

run_file_test "mad" "south.mp3" "mad"
run_file_test "mad/lame" "south.mp3" "mad ! lame"
run_file_test "mad/lame/mad" "south.mp3" "mad ! lame ! mad"
run_file_test "vorbisdec" "Brown\ Sugar128.ogg" "vorbisdec"
run_file_test "vorbisdec/vorbisenc" "Brown\ Sugar128.ogg" "vorbisdec ! vorbisenc"

