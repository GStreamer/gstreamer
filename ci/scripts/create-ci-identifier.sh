#! /bin/bash

# Create a file we can check at runtime, and identify if the environment we
# run against is one of our CI build images.
# Useful mostly for our internal scripts so we can match against metadata
# rather than heuristics, ex. if /subprojects exists
#
# Conceptually similar to /.flatpak-info
# We can also later on add various metadata once we need to.
# Preferebly in an .ini format
touch /.gstreamer-ci-linux-image
chmod 644 /.gstreamer-ci-linux-image
