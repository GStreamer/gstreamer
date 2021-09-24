# -*- Mode: Python -*- vi:si:et:sw=4:sts=4:ts=4:syntax=python
#
# Copyright (c) 2014,Thibault Saunier <thibault.saunier@collabora.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

"""
The GstValidate default testsuite
"""

import os
from testsuiteutils import update_assets


TEST_MANAGER = "validate"

BLACKLIST = [("validateextra.*reverse.*Sintel_2010_720p_mkv",
             "TODO in matroskademux: FIXME: We should build an index during playback or "
             "when scanning that can be used here. The reverse playback code requires "
             " seek_index and seek_entry to be set!"),

            # Subtitles known issues
            ("validateextra.file.playback.switch_subtitle_track.Sintel_2010_720p_mkv",
             "https://bugzilla.gnome.org/show_bug.cgi?id=734051"),
            ("validateextra.rtsp.*subtitle.*Sintel_2010_720p_mkv$",
             "Subtitles are not exposed on RTSP?")
]

def setup_tests(test_manager, options):
    assets_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "medias", "big"))
    if options.sync:
        if not update_assets(options, assets_dir):
            return False

    options.add_paths(assets_dir)
    test_manager.set_default_blacklist(BLACKLIST)
    test_manager.register_defaults()

    return True
