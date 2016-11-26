#!/bin/sh
# ----------------------------------------------------------------------------
#
# create-uninstalled-setup.sh
#
# Little shell script that creates a fresh GStreamer uninstalled setup in
# your home directory.
#
# ----------------------------------------------------------------------------
#
# Copyright (C) 2011-2015 Tim-Philipp Müller <tim centricular net>
#
# This script is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.
#
# ----------------------------------------------------------------------------

set -e

# set BRANCH to e.g. "1.2" to track the stable 1.2 branch instead of master
BRANCH="master"

# set to "ssh" if you have a developer account and ssh access
GIT_ACCESS="anongit"

# extra clone options
#CLONE_OPTS="--depth=1 --no-single-branch"

# re-use and reference local master branch checkout if one already exists
# (saves network bandwidth)
REUSE_EXISTING_MASTER_CHECKOUT="true"

# git modules to clone
MODULES="gstreamer gst-plugins-base gst-plugins-good gst-plugins-ugly gst-plugins-bad gst-libav"

# note: we use ~/gst because that's what the gst-uninstalled script
# uses by default, so don't just change that to something else
UNINSTALLED_ROOT=~/gst

echo "==========================================================================================="
echo "Creating new GStreamer uninstalled environment for branch $BRANCH in $UNINSTALLED_ROOT ... "
echo "==========================================================================================="

mkdir -p $UNINSTALLED_ROOT
mkdir -p $UNINSTALLED_ROOT/$BRANCH

mkdir -p $UNINSTALLED_ROOT/$BRANCH/prefix


echo ""
echo "Checking basic build tools and dependencies are installed..."
echo ""

if ! pkg-config --version 2>/dev/null >/dev/null; then
  DEPS_OK="no"
elif ! pkg-config --exists glib-2.0 orc-0.4 2>/dev/null >/dev/null; then
  DEPS_OK="no"
elif ! bison --version 2>/dev/null >/dev/null; then
  DEPS_OK="no"
elif ! flex --version 2>/dev/null >/dev/null; then
  DEPS_OK="no"
elif ! git --version 2>/dev/null >/dev/null; then
  DEPS_OK="no"
else
  DEPS_OK="yes"
fi

if test "$DEPS_OK" != "yes"; then
echo "==========================================================================================="
echo ""
echo "  Some very basic build tools or dependencies are missing."
echo ""
echo "  Please install the following tools: pkg-config, bison, flex, git"
echo ""
echo "  and the following libraries: GLib (libglib2.0-dev or glib2-devel)"
echo "                           and Orc  (liborc-0.4-dev or orc-devel)"
#echo "  Please visit"
#echo ""
#echo "      http://gstreamer.freedesktop.org/wiki/BuildDependenciesDebianUbuntu"
#echo ""
#echo "         or"
#echo ""
#echo "      http://gstreamer.freedesktop.org/wiki/Fedora"
#echo ""
#echo "  for instructions how to install them on Debian/Ubuntu-based systems."
echo ""
echo "==========================================================================================="
exit 1
fi


cd $UNINSTALLED_ROOT/$BRANCH

for m in $MODULES
do
  REF=""
  if test "$BRANCH" != "master" \
    -a "x$REUSE_EXISTING_MASTER_CHECKOUT" = "xtrue" \
    -a -d ../master/$m; then
      REF="--reference=../master/$m"
  fi

  if test "$GIT_ACCESS" = "ssh"; then
    git clone $CLONE_OPTS $REF ssh://git.freedesktop.org/git/gstreamer/$m
  else
    git clone $CLONE_OPTS $REF https://anongit.freedesktop.org/git/gstreamer/$m
  fi

  cd $m
  if test "$BRANCH" != "master"; then
    git checkout -b $BRANCH origin/$BRANCH
  fi
  git submodule init && git submodule update
  cd ..
done

cd $UNINSTALLED_ROOT
ln -s $BRANCH/gstreamer/scripts/gst-uninstalled gst-$BRANCH
chmod +x gst-$BRANCH

cd ~

echo "==========================================================================================="
echo
echo "Done. Created new GStreamer uninstalled environment for branch $BRANCH in $UNINSTALLED_ROOT"
echo
echo "To enter the uninstalled environment do: cd $UNINSTALLED_ROOT; ./gst-$BRANCH"
echo
echo "To leave the uninstalled environment do: exit"
echo
echo "To check the uninstalled environment do: printenv | grep GST"
echo "    (loads of output = you're in the uninstalled environment)"
echo
echo "==========================================================================================="
echo
echo "Now compile all GStreamer modules one by one by first switching into"
echo "the uninstalled environment and then doing:"
echo
echo "    cd <MODULE>; ./autogen.sh; make"
echo
echo "First gstreamer, then gst-plugins-base, then the other modules."
echo "You do not need to do 'make install'"
echo
echo "==========================================================================================="
echo
echo "If your system GLib is too old, you can install a newer version"
echo "into --prefix=$UNINSTALLED_ROOT/$BRANCH/prefix and it should be picked up"
echo "by autogen.sh/configure"
echo
echo "==========================================================================================="
#echo
#echo "Also see http://gstreamer.freedesktop.org/wiki/UninstalledSetup"
#echo
#echo "==========================================================================================="

