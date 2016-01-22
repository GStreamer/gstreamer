#!/bin/sh
# simple benchmark to check the overhead of the tracers
#
# tracers can be a list of tracers separated using ';'

if [ -z "$1" ]; then
  echo "Usage: $0 <tracer(s)> [<file>]"
  exit 1 
fi

tracer=$1

if [ -z "$2" ]; then
  file=$(ls -1R $HOME/Music/ | grep -v "/:" | head -n1)
  file=$(ls $HOME/Music/$file)
else
  file=$2
fi

echo "testing $tracer on $file"
cat $file >/dev/null

log=`mktemp`

function test() {
  GST_DEBUG_FILE="$log" /usr/bin/gst-launch-1.0 playbin uri=file://$file audio-sink="fakesink sync=false" video-sink="fakesink sync=false" | grep "Execution ended after" | sed 's/Execution ended after//'
}

echo "$tracer"
GST_DEBUG="GST_TRACER:7" GST_TRACERS="$tracer" test
GST_DEBUG=

echo "no-log"
GST_TRACERS="$tracer" test
GST_TRACERS=

echo "reference"
test

rm "$log"
