#!/bin/sh
# this script provides feedback for GStreamer debugging
# the user can run this and provide the GStreamer developers with information
# about their system

command_output ()
{
  echo "+++ $1"
  $1
}

echo "GStreamer feedback script."
echo "Please attach the output of this script to your bug reports."
echo "Bug reports should go into Gnome's bugzilla (http://bugzilla.gnome.org)"
echo

echo "+   SYSTEM INFORMATION"
command_output "uname -a"

if test -f /etc/redhat-release; then
  echo "+++  distribution: Red Hat"
  cat /etc/redhat-release
fi

if test -f /etc/debian_version; then
  echo "+++  distribution: Debian"
  cat /etc/debian_version
fi

command_output "cat /etc/issue"

echo

echo "+   USER INFORMATION"
command_output "id"
echo

echo "+   PKG-CONFIG INFORMATION"
command_output "pkg-config --version"
command_output "pkg-config gstreamer --modversion"
command_output "pkg-config gstreamer --cflags"
command_output "pkg-config gstreamer --libs"
command_output "pkg-config gstreamer-libs --modversion"
command_output "pkg-config gstreamer-libs --cflags"
command_output "pkg-config gstreamer-libs --libs"
echo

echo "+   GSTREAMER INFORMATION"
command_output "which gst-register"
command_output "gst-inspect"
command_output "gst-inspect fakesrc"
command_output "gst-inspect fakesink"
command_output "gst-launch fakesrc num_buffers=5 ! fakesink"

echo "++  looking for gstreamer libraries in common locations"
for dirs in /usr/lib /usr/local/lib /home; do
  if test -d $dirs; then
    find $dirs -name libgstreamer* | grep so
  fi
done
echo "++  looking for gstreamer headers in common locations"
for dirs in /usr/include /usr/local/include /home; do
  if test -d $dirs; then
    find $dirs -name gst.h
  fi
done

echo "+   GSTREAMER PLUG-INS INFORMATION"
command_output "gst-inspect volume"

echo "++  looking for gstreamer volume plugin in common locations"
for dirs in /usr/lib /usr/local/lib /home; do
  if test -d $dirs; then
    find $dirs -name libgstvolume* | grep so
  fi
done
echo "++  looking for gstreamer headers in common locations"
for dirs in /usr/include /usr/local/include /home; do
  if test -d $dirs; then
    find $dirs -name audio.h
  fi
done


