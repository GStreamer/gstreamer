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
from loggable import Loggable
from optparse import OptionGroup

from utils import mkdir, Result, Colors, printc, DEFAULT_TIMEOUT


class Test(Loggable):

    """ A class representing a particular test. """

    def __init__(self, application_name, classname, options,
                 reporter, timeout=DEFAULT_TIMEOUT):
        Loggable.__init__(self)
        self.timeout = timeout
        self.classname = classname
        self.options = options
        self.application = application_name
        self.command = ""
        self.reporter = reporter
        self.process = None

        self.message = ""
        self.error = ""
        self.time_taken = 0.0
        self._starting_time = None
        self.result = Result.NOT_RUN
        self.logfile = None

    def __str__(self):
        string = self.classname
        if self.result != Result.NOT_RUN:
            string += ": " + self.result
            if self.result in [Result.FAILED, Result.TIMEOUT]:
                string += " '%s'\n       You can reproduce with: %s\n       " \
                    "You can find logs in: %s" % (self.message, self.command,
                                                  self.logfile)

        return string

    def get_classname(self):
        name = self.classname.split('.')[-1]
        classname = self.classname.replace('.%s' % name, '')

        return classname

    def get_name(self):
        return self.classname.split('.')[-1]

    def add_arguments(self, *args):
        for arg in args:
            self.command += " " + arg

    def build_arguments(self):
        pass

    def set_result(self, result, message="", error=""):
        self.result = result
        self.message = message
        self.error = error

    def check_results(self):
        if self.result is Result.FAILED:
            return

        self.debug("%s returncode: %s", self, self.process.returncode)
        if self.result == Result.TIMEOUT:
            self.set_result(Result.TIMEOUT, "Application timed out", "timeout")
        elif self.process.returncode == 0:
            self.result = Result.PASSED
        else:
            self.set_result(Result.FAILED,
                            "Application returned %d" % (
                            self.process.returncode))

    def get_current_value(self):
        """
        Lets subclasses implement a nicer timeout measurement method
        They should return some value with which we will compare
        the previous and timeout if they are egual during self.timeout
        seconds
        """
        return Result.NOT_RUN

    def wait_process(self):
        last_val = 0
        last_change_ts = time.time()
        while True:
            self.process.poll()
            if self.process.returncode is not None:
                break

            # Dirty way to avoid eating to much CPU...
            # good enough for us anyway.
            time.sleep(1)

            val = self.get_current_value()

            if val is Result.NOT_RUN:
                # The get_current_value logic is not implemented... dumb timeout
                if time.time() - last_change_ts > self.timeout:
                    self.result = Result.TIMEOUT
                    break
                continue
            elif val is Result.FAILED:
                self.result = Result.FAILED
                break

            self.log("New val %s" % val)

            if val == last_val:
                delta = time.time() - last_change_ts
                self.debug("Same value for %d seconds" % delta)
                if delta > self.timeout:
                    self.result = Result.TIMEOUT
                    break
            else:
                last_change_ts = time.time()
                last_val = val

        self.check_results()

    def run(self):
        self.command = "%s " % (self.application)
        self._starting_time = time.time()
        self.build_arguments()
        printc("Launching: %s%s\n"
               "           logs are in %s\n"
               "           Command: '%s'\n"
               % (Colors.ENDC, self.classname,
                  self.logfile, self.command), Colors.OKBLUE)
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

        self.reporter.out.seek(0)
        self.reporter.out.write("=================\n"
                                "Test name: %s\n"
                                "Command: '%s'\n"
                                "=================\n\n"
                                % (self.classname, self.command))

        return self.result


class GstValidateTest(Test):

    """ A class representing a particular test. """

    def __init__(self, application_name, classname,
                 options, reporter, timeout=DEFAULT_TIMEOUT,
                 scenario=None):

        super(GstValidateTest, self).__init__(application_name, classname, options,
                                              reporter, timeout=DEFAULT_TIMEOUT)

        if scenario is None or scenario.lower() == "none":
            self.scenario = None
        else:
            self.scenario = scenario

    def build_arguments(self):
        if self.scenario is not None:
            self.add_arguments("--set-scenario", self.scenario)

    def get_validate_criticals_errors(self):
        self.reporter.out.seek(0)
        ret = "["
        errors = []
        for l in self.reporter.out.readlines():
            if "critical : " in l:
                if ret != "[":
                    ret += ", "
                error = l.split("critical : ")[1].replace("\n", '')
                print "%s -- %s" % (error, errors)
                if error not in errors:
                    ret += error
                    errors.append(error)

        if ret == "[":
            return "No critical"
        else:
            return ret + "]"

    def check_results(self):
        if self.result is Result.FAILED:
            return

        self.debug("%s returncode: %s", self, self.process.returncode)
        if self.result == Result.TIMEOUT:
            self.set_result(Result.TIMEOUT, "Application timed out", "timeout")
        elif self.process.returncode == 0:
            self.result = Result.PASSED
        else:
            if self.process.returncode == 139:
                # FIXME Reimplement something like that if needed
                # self.get_backtrace("SEGFAULT")
                self.set_result(Result.FAILED,
                                "Application segfaulted",
                                "segfault")
            else:
                self.set_result(Result.FAILED,
                                "Application returned %d (issues: %s)" % (
                                self.process.returncode,
                                self.get_validate_criticals_errors()
                                ))


class TestsManager(Loggable):

    """ A class responsible for managing tests. """

    name = ""

    def __init__(self):

        Loggable.__init__(self)

        self.tests = []
        self.options = None
        self.args = None
        self.reporter = None
        self.wanted_tests_patterns = []

    def init(self):
        return False

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
        if not self.wanted_tests_patterns:
            return True

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

    def needs_http_server(self):
        return False


class _TestsLauncher(Loggable):
    def __init__(self):

        Loggable.__init__(self)

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
                    if issubclass(symb[1], c) and not symb[1] is c:
                        subclasses.append(symb[1])
                except TypeError:
                    pass

            return subclasses

        env = globals().copy()
        d = os.path.dirname(__file__)
        for f in os.listdir(os.path.join(d, "apps")):
            if f.endswith(".py"):
                execfile(os.path.join(d, "apps", f), env)

        testers = [i() for i in get_subclasses(TestsManager, env)]
        for tester in testers:
            if tester.init() is True:
                self.testers.append(tester)
            else:
                self.warning("Can not init tester: %s -- PATH is %s"
                             % (tester.name, os.environ["PATH"]))

    def add_options(self, parser):
        for tester in self.testers:
            group = OptionGroup(parser, "%s Options" % tester.name,
                                "Options specific to the %s test manager"
                                % tester.name)
            tester.add_options(group)
            parser.add_option_group(group)

    def set_settings(self, options, args):
        self.reporter = reporters.XunitReporter(options)
        mkdir(options.logsdir)

        wanted_testers = None
        for tester in self.testers:
            if tester.name in args:
                wanted_testers = tester.name
        if wanted_testers:
            testers = self.testers
            self.testers = []
            for tester in testers:
                if tester.name in args:
                    self.testers.append(tester)
                    args.remove(tester.name)

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

    def needs_http_server(self):
        for tester in self.testers:
            if tester.needs_http_server():
                return True
