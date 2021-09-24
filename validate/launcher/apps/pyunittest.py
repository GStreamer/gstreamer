#!/usr/bin/env python3
#
# Copyright (c) 2018,Thibault Saunier <tsaunier@igalia.com>
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
import os
import sys
import unittest

from launcher.baseclasses import Test
from launcher.baseclasses import TestsManager


class PythonTest(Test):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._testname = self.classname

    def build_arguments(self):
        """Builds subprocess arguments."""
        self.add_arguments('-m', 'unittest', self._testname)


class PythonTestsManager(TestsManager):
    name = "pyunittest"
    arggroup = None

    def __init__(self):
        super().__init__()

    def add_options(self, parser):
        if self.arggroup:
            return

        arggroup = PythonTestsManager.arggroup = parser.add_argument_group(
            "Python tests specific options and behaviours")
        arggroup.add_argument("--pyunittest-dir",
                              action="append",
                              default=[],
                              help="Paths to look for Python tests.")

    def list_tests(self):
        if self.tests:
            return self.tests

        for _dir in self.options.pyunittest_dir:
            loader = unittest.TestLoader()
            testsuites = loader.discover(_dir)
            for testsuite in testsuites:
                for _tests in testsuite:
                    if isinstance(_tests, unittest.loader._FailedTest):
                        raise(_tests._exception)
                    for test in _tests:
                        self.add_test(PythonTest(
                            sys.executable, test.id(),
                            self.options, self.reporter,
                            extra_env_variables={'PYTHONPATH': _dir}))

        return self.tests
