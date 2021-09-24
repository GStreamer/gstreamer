# Pastebin PJeVxRyU
# -*- Mode: Python -*- vi:si:et:sw=4:sts=4:ts=4:syntax=python
#
# Copyright (c) 2014 Thibault Saunier <thibault.saunier@collabora.com>
# Copyright (c) 2017 Sebastian Droege <sebastian@centricular.com>
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
The GstValidate adaptive streams test-vectors testsuite
"""

import os
from launcher.baseclasses import MediaFormatCombination

TEST_MANAGER = "validate"

BLACKLIST = [
    ('validate.dash.playback.trick_mode_seeks.DASHIF_TestCases_2a_qualcomm_1_MultiResMPEG2',
     'https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/545'),
    ('validate.dash.playback.trick_mode_seeks.DASHIF_TestCases_1a_netflix_exMPD_BIP_TC1',
     'https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/545'),
]


def setup_tests(test_manager, options):
    print("Setting up GstValidate Adaptive Streaming test-vectors tests")

    assets_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "medias", "adaptivecontent"))
    options.add_paths(assets_dir)
    test_manager.set_default_blacklist(BLACKLIST)
    # test_manager.add_expected_issues(EXPECTED_ISSUES)
    test_manager.register_defaults()
    scenarios = test_manager.get_scenarios()
    # Don't test generic tests that are already covered by the base validate runner
    scenarios.remove("change_state_intensive")
    # Scrubbing is a tad pointless/intensive for these suites (already covered elsewhere)
    scenarios.remove("scrub_forward_seeking")
    # Add keyunit trick mode testing
    scenarios.append("trick_mode_seeks")
    # Add live/seekable scenarios
    scenarios.append("seek_end_live")
    scenarios.append("full_live_rewind")
    scenarios.append("play_15s_live")
    test_manager.set_scenarios(scenarios)

    return True
