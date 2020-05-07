# -*- Mode: Python -*- vi:si:et:sw=4:sts=4:ts=4:syntax=python
#
# Copyright (c) 2016,Thibault Saunier <thibault.saunier@osg.samsung.com>
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
from launcher.apps.gstvalidate import GstValidateSimpleTestsGenerator

TEST_MANAGER = "validate"


def get_pipelines(test_manager):
    return [("not_negotiated.accept_caps_failure",
             "audiotestsrc ! audio/x-raw,channels=2,channel-mask='(bitmask)0x67' "
             "! audioconvert ! capsfilter caps=audio/x-raw,channels=6,channel-mask='(bitmask)0x32' "
             " name=capsfilter ! fakesink",
             {"expected-issues": [
                 {'returncode': 18},
                 {'level': 'critical', 'summary': 'a NOT NEGOTIATED message has been posted on the bus.',
                  'details': r'.*Caps negotiation failed at pad.*capsfilter:sink.*as it refused caps:.*'}]}),
            ("not_negotiated.caps_query_failure",
             "\( \( audiotestsrc \) ! input-selector name=i \) ! capsfilter name=capsfilter caps=video/x-raw ! fakesink",
             {"expected-issues": [
                 {'returncode': 18},
                 {'level': 'critical', 'summary': 'a NOT NEGOTIATED message has been posted on the bus.',
                  'details': 'Caps negotiation failed starting from pad \'capsfilter:sink\' as the '
                             'QUERY_CAPS returned EMPTY caps for the following possible reasons:'}]})]


def setup_tests(test_manager, options):
    testsuite_dir = os.path.realpath(os.path.join(os.path.dirname(__file__)))

    print("Setting up tests to test GstValidate")
    # No restriction about scenarios that are potentially used
    valid_scenarios = ["play_15s"]
    test_manager.add_scenarios(valid_scenarios)
    test_manager.add_generators(test_manager.GstValidatePipelineTestsGenerator
                                ("test_validate", test_manager,
                                 pipelines_descriptions=get_pipelines(test_manager),
                                 valid_scenarios=valid_scenarios))

    test_manager.add_generators(
        GstValidateSimpleTestsGenerator("simple", test_manager,
            os.path.join(testsuite_dir))
    )

    return True
