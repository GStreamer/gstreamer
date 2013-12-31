#!/usr/bin/python
#
# Copyright (c) 2013,Thibault Saunier <thibault.saunier@collabora.com>
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

""" Class representing tests and test managers. """

import os
import re
import time
import subprocess
import reporters

from utils import mkdir, Result

DEFAULT_TIMEOUT = 10


class Test(object):

    """ A class representing a particular test. """

    def __init__(self, application_name, classname, options, reporter):
        self.timeout = DEFAULT_TIMEOUT
        self.classname = classname
        self.options = options
        self.application = application_name
        self.command = ""
        self.reporter = reporter
        self.process = None

        self.message = None
        self.error = None
        self.time_taken = None
        self._starting_time = None
        self.result = Result.NOT_RUN

    def __str__(self):
        string = self.classname
        if self.result:
            string += ": " + self.result
            if "FAILED" in self.result:
                string += "\n       You can reproduce with: " + self.command

        return string

    def add_arguments(self, *args):
        for arg in args:
            self.command += " " + arg

    def build_arguments(self):
        pass

    def set_result(self, result, message=None, error=None):
        print "SETTING TER"
        self.result = result
        self.message = message
        self.error = error

    def check_results(self):
        if self.process.returncode == 0:
            self.result = Result.PASSED

        self.result = Result.FAILED

    def wait_process(self):
        last_change_ts = time.time()
        while True:
            self.process.poll()
            if self.process.returncode is not None:
                break

            if time.time() - last_change_ts > self.timeout:
                self.result = Result.TIMEOUT

            # Dirty way to avoid eating to much CPU...
            # good enough for us anyway.
            time.sleep(1)

        self.check_results()

    def get_validate_criticals_errors(self):
        self.reporter.out.seek(0)
        ret = "["
        for l in self.reporter.out.readlines():
            if "critical : " in l:
                if ret != "[":
                    ret += ", "
                ret += l.split("critical : ")[1].replace("\n", '')

        if ret == "[":
            return "No critical"
        else:
            return ret + "]"

    def run(self):
        self.command = "%s " % (self.application)
        self._starting_time = time.time()
        self.build_arguments()
        print "Launching %s" % self.command
        try:
            self.process = subprocess.Popen(self.command,
                                            stderr=self.reporter.out,
                                            stdout=self.reporter.out,
                                            shell=True)
            self.wait_process()
        except KeyboardInterrupt:
            self.process.kill()
            raise

        try:
            self.process.terminate()
        except OSError:
            pass

        self.time_taken = time.time() - self._starting_time


class TestsManager(object):

    """ A class responsible for managing tests. """

    def __init__(self):
        self.tests = []
        self.options = None
        self.args = None
        self.reporter = None
        self.wanted_tests_patterns = []

    def list_tests(self):
        pass

    def get_tests(self):
        return self.tests

    def add_options(self, parser):
        """ Add more arguments. """
        pass

    def set_settings(self, options, args, reporter):
        """ Set properties after options parsing. """
        self.options = options
        self.args = args
        self.reporter = reporter

        if options.wanted_tests:
            for pattern in options.wanted_tests.split(','):
                self.wanted_tests_patterns.append(re.compile(pattern))


    def _is_test_wanted(self, test):
        for pattern in self.wanted_tests_patterns:
            if pattern.findall(test.classname):
                return True

        return False

    def run_tests(self):
        for test in self.tests:
            if self._is_test_wanted(test):
                self.reporter.before_test(test)
                test.run()
                self.reporter.after_test()


class _TestsLauncher(object):
    def __init__(self):
        self.testers = []
        self.tests = []
        self.reporter = None
        self._list_testers()
        self.wanted_tests_patterns = []

    def _list_testers(self):
        def get_subclasses(c, env):
            subclasses = []
            for symb in env.iteritems():
                try:
                    if issubclass(symb[1], c):
                        subclasses.append(symb[1])
                except TypeError:
                    pass

            return subclasses

        env = globals().copy()
        d = os.path.dirname(__file__)
        for f in os.listdir(os.path.join(d, "apps")):
            execfile(os.path.join(d, "apps", f), env)
        self.testers = [i() for i in get_subclasses(TestsManager, env)]
        print self.testers

    def add_options(self, parser):
        for tester in self.testers:
            tester.add_options(parser)

    def set_settings(self, options, args):
        self.reporter = reporters.XunitReporter(options)
        mkdir(options.logsdir)
        for tester in self.testers:
            tester.set_settings(options, args, self.reporter)

    def list_tests(self):
        for tester in self.testers:
            tester.list_tests()
            self.tests.extend(tester.tests)

    def run_tests(self):
        for tester in self.testers:
            tester.run_tests()

    def final_report(self):
        self.reporter.final_report()
