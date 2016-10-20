#!/bin/bash
# dumps a gnuplot script to stdout that plot of the given log

usage="\
Usage:$0 [--title=<title>] [--log=<log>] [--format={png,pdf,ps,svg}] [--pagesize={a3,a4}]| gnuplot"

# default options
title="GStreamer trace"
log="trace.log"
format="png"
pagesize="a3"

# process commandline options
# @todo: add support for single letter options
while true; do
  case "X$1" in
    X--version) echo "0.1"; exit 0;;
    X--help) echo "$usage"; exit 0;;
    X--title=*) title=`echo $1 | sed s/.*=//`; shift;;
    X--log=*) log=`echo $1 | sed s/.*=//`; shift;;
    X--format=*) format=`echo $1 | sed s/.*=//`; shift;;
    X--pagesize=*) pagesize=`echo $1 | sed s/.*=//`; shift;;
    X--*) shift;;
    X*) break;;
  esac
done

tmp=`mktemp -d`

plot_width=1600
plot_height=1200

base=`basename "$log" ".log"`

# filter log
grep "TRACE" $log | grep "GST_TRACER" >$tmp/trace.log
log=$tmp/trace.log

grep -o "proc-rusage,.*" $log | cut -c14- | sed  -e 's#process-id=(guint64)[0-9][0-9]*, ##' -e 's#ts=(guint64)##' -e 's#[a-z]*-cpuload=(uint)##g' -e 's#time=(guint64)##' -e 's#;##' -e 's#, # #g' >$tmp/cpu_proc.dat
grep -o "thread-rusage,.*" $log | cut -c35- | sed -e 's#ts=(guint64)##' -e 's#thread-id=(uint)##g'  -e 's#[a-z]*-cpuload=(uint)##g' -e 's#time=(guint64)##' -e 's#;##' -e 's#, # #g' >$tmp/cpu_threads.dat
( cd $tmp; awk -F" " '{ print $2, $3, $4, $5 >"cpu_thread."$1".dat" }' cpu_threads.dat )

# configure output
# http://en.wikipedia.org/wiki/Paper_size
case $pagesize in
  a3) page_with="29.7 cm";page_height="42.0 cm";;
  a4) page_with="21.0 cm";page_height="29.7 cm";;
esac
# http://www.gnuplot.info/docs/node341.html (terminal options)
case $format in
  # this doen't like fonts
  png) echo "set term png truecolor font \"Sans,7\" size $plot_width,$plot_height";;
  # pdf makes a new page for each plot :/
  pdf) echo "set term pdf color font \"Sans,7\" size $page_with,$page_height";;
  ps) echo "set term postscript portrait color solid \"Sans\" 7 size $page_with,$page_height";;
  svg) echo "set term svg size $plot_width,$plot_height font \"Sans,7\"";;
esac
cat <<EOF
set output '$base.cpu.$format'
set xlabel "Time (ns)"
set ylabel "Per-Mille"
set grid
plot \\
  '$tmp/cpu_proc.dat' using 1:2 with lines title 'avg cpu', \\
  '' using 1:3 with lines title 'cur cpu'

set output '$base.thread.$format'
set xlabel "Time (ns)"
set ylabel "Per-Mille"
set grid
plot \\
EOF
for file in $tmp/cpu_thread.*.dat ; do
  ct=`cat $file | wc -l`
  if [ $ct -lt 100 ]; then
    continue
  fi
  id=`echo $file | sed 's#.*cpu_thread.\([0-9]*\).dat#\1#'`
  cat <<EOF
  '$file' using 1:2 with lines title '$id avg cpu', \\
  '' using 1:3 with lines title '$id cur cpu', \\
EOF
done
cat <<EOF

EOF

