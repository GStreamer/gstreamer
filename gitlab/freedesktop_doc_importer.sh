#!/bin/sh

set -e

BRANCH=master
NAMESPACE=gstreamer
JOB=documentation

WORK_DIR=`mktemp -d -p "$DIR"`

# deletes the temp directory
function cleanup {
  rm -rf "$WORK_DIR"
  echo "Deleted temp working directory $WORK_DIR"
}

# register the cleanup function to be called on the EXIT signal
trap cleanup EXIT

echo ""
echo "============================================================================================================================"
echo "Updating documentation from: https://gitlab.freedesktop.org/$NAMESPACE/gst-docs/-/jobs/artifacts/$BRANCH/download?job=$JOB"

date

cd $WORK_DIR
wget https://gitlab.freedesktop.org/$NAMESPACE/gst-docs/-/jobs/artifacts/$BRANCH/download?job=$JOB -O gstdocs.zip

unzip gstdocs.zip

DOC_BASE="/srv/gstreamer.freedesktop.org/public_html/documentation"

rsync -rvaz --links --delete documentation/ $DOC_BASE  || /bin/true
chmod -R g+w $DOC_BASE; chgrp -R gstreamer $DOC_BASE

echo "Done updating documentation"
echo ""