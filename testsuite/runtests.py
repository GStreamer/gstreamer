#!/usr/bin/env python3
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2002 David I. Lehn
# Copyright (C) 2004 Johan Dahlin
# Copyright (C) 2005 Edward Hervey
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

import os
import sys
import unittest


def _testcases(filenames):
    """Yield testcases out of filenames."""
    for filename in filenames:
        if filename.endswith(".py"):
            yield filename[:-3]


def _tests_suite():
    """Pick which tests to run."""
    testcase = os.getenv("TESTCASE")
    if testcase:
        testcases = [testcase]
    else:
        testcases = _testcases(sys.argv[1:])
    loader = unittest.TestLoader()
    return loader.loadTestsFromNames(testcases)


def setup():
    return


if __name__ == "__main__":
    setup()

    # Set verbosity.
    descriptions = 1
    verbosity = 1
    if 'VERBOSE' in os.environ:
        descriptions = 2
        verbosity = 2

    suite = _tests_suite()
    if not list(suite):
        raise Exception("No tests found")

    # Run the tests.
    testRunner = unittest.TextTestRunner(descriptions=descriptions,
                                         verbosity=verbosity)
    result = testRunner.run(suite)
    if result.failures or result.errors:
        sys.exit(1)
