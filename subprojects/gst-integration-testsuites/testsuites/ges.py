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
The GES GstValidate default testsuite
"""
import os
import pathlib
import subprocess
from launcher import utils
from testsuiteutils import update_assets
from ges_known_issues import KNOWN_ISSUES


TEST_MANAGER = "ges"


def setup_tests(test_manager, options):
    assets_dir = os.path.abspath(os.path.join(
        os.path.dirname(__file__), "..", "medias", "defaults"))
    if options.sync:
        if not utils.USING_SUBPROJECT:
            if not update_assets(options, assets_dir):
                return False
        else:
            print("Syncing gst-integration-testsuites media files")
            subprocess.check_call(['git', 'submodule', 'update', '--init'],
                                  cwd=utils.DEFAULT_GST_QA_ASSETS)
            subprocess.check_call(['git', 'lfs', 'pull', '--exclude='],
                                  cwd=pathlib.Path(utils.DEFAULT_GST_QA_ASSETS) / 'medias')
    options.add_paths(os.path.abspath(os.path.join(os.path.dirname(__file__),
                                                   "..", "medias", "defaults")))
    projects_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "ges",
                                                 "ges-projects"))
    scenarios_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "ges",
                                                  "scenarios"))
    test_manager.add_expected_issues(KNOWN_ISSUES)
    test_manager.register_defaults(projects_path, scenarios_path)
    return True
