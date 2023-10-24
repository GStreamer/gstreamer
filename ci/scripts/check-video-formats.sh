#! /bin/bash

set -eux

meson devenv -C build -w . ./scripts/sort_video_formats.py ${VIDEO_TOKEN} ${VIDEO_HEADER}
meson devenv -C build -w . ./scripts/sort_video_formats.py -b ${WL_TOKEN} ${WL_HEADER}
