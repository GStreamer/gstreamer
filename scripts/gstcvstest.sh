#!/bin/bash
# gstreamer auto-builder
# 0.1.0
# thomas@apestaart.org
# check out fresh gstreamer cvs code anonymously, configure, build

# FIXME :
# * check out source twice, compare, to make sure we don't get code
#   in between commits
# * add rpm building if allowed
# * maybe change dir where stuff gets built ?


BR=/tmp		# build root
export DISPLAY=:0.0 # gtk-scandoc needs an X server

# delete logs
rm -rf $BR/*.log

echo -n "+ Starting on "
date
echo -n "+ "
uname -a
# delete gstreamer dir if it exists
if test -e $BR/gstreamer
then
  echo "+ Deleting $BR/gstreamer"
  chmod u+rwx -R /tmp/gstreamer
  rm -rf $BR/gstreamer
fi

cd $BR

# check out
echo "+ Checking out source code"
cvs -z3 -d:pserver:anonymous@cvs.gstreamer.sourceforge.net:/cvsroot/gstreamer co gstreamer > cvs.log 2>&1

# do your thing
cd gstreamer

# autogen
echo "+ Running ./autogen.sh"
./autogen.sh  > ../autogen.log 2>&1
if test $? -ne 0
then
  echo "- Problem while running autogen.sh"
  echo "- Dumping end of log ..."
  echo
  tail -n 20 ../autogen.log
  exit
fi

echo "+ Running ./configure --enable-docs-build=no"
./configure --enable-docs-build=no > ../configure.log 2>&1
if test $? -ne 0
then
  echo "- Problem while running configure"
  echo "- Dumping end of log ..."
  echo
  tail -n 20 ../configure.log
  exit
fi

# make
echo "+ Running make"
make > ../make.log 2>&1
if test $? -ne 0
then
  echo "- Problem while running make"
  echo "- Dumping end of log ..."
  echo
  grep -v "pasting would not give a valid" ../make.log > ../make.scrubbed.log
  tail -n 20 ../make.scrubbed.log
  exit
fi

echo "+ Running BUILD_DOCS= make distcheck"
BUILD_DOCS= make distcheck > ../makedistcheck.log 2>&1
if test $? -ne 0
then
  echo "- Problem while running make distcheck"
  echo "- Dumping end of log ..."
  echo
  tail -n 20 ../makedistcheck.log
  exit
fi

echo -n "+ Ending successful build cycle on "
date
