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
import utils
import urlparse
import subprocess
import reporters
from loggable import Loggable
from optparse import OptionGroup

from utils import mkdir, Result, Colors, printc, DEFAULT_TIMEOUT


class Test(Loggable):

    """ A class representing a particular test. """

    def __init__(self, application_name, classname, options,
                 reporter, timeout=DEFAULT_TIMEOUT, hard_timeout=None):
        """
        @timeout: The timeout during which the value return by get_current_value
                  keeps being exactly equal
        @hard_timeout: Max time the test can take in absolute
        """
        Loggable.__init__(self)
        self.timeout = timeout
        self.hard_timeout = hard_timeout
        self.classname = classname
        self.options = options
        self.application = application_name
        self.command = ""
        self.reporter = reporter
        self.process = None

        self.message = ""
        self.error_str = ""
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
        self.error_str = error

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
        start_ts = time.time()
        while True:
            self.process.poll()
            if self.process.returncode is not None:
                break

            # Dirty way to avoid eating to much CPU...
            # good enough for us anyway.
            time.sleep(1)

            val = self.get_current_value()

            self.debug("Got value: %s" % val)
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
                self.debug("%s: Same value for %d/%d seconds" % (self, delta, self.timeout))
                if delta > self.timeout:
                    self.result = Result.TIMEOUT
                    break
            elif self.hard_timeout and time.time() - start_ts > self.hard_timeout:
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
               "           Command: '%s'"
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
        printc("Result: %s%s\n" % (self.result,
               " (" + self.message + ")" if self.message else ""),
               color=utils.get_color_for_result(self.result))

        return self.result


class GstValidateTest(Test):

    """ A class representing a particular test. """

    def __init__(self, application_name, classname,
                 options, reporter, timeout=DEFAULT_TIMEOUT,
                 scenario=None, hard_timeout=None, max_outside_segment=5):

        super(GstValidateTest, self).__init__(application_name, classname, options,
                                              reporter, timeout=timeout, hard_timeout=hard_timeout)

        # defines how much the process can be outside of the configured
        # segment / seek
        self.max_outside_segment = max_outside_segment

        if scenario is None or scenario.name.lower() == "none":
            self.scenario = None
        else:
            self.scenario = scenario

    def build_arguments(self):
        if self.scenario is not None:
            self.add_arguments("--set-scenario", self.scenario.name)

    def get_validate_criticals_errors(self):
        self.reporter.out.seek(0)
        ret = "["
        errors = []
        for l in self.reporter.out.readlines():
            if "critical : " in l:
                if ret != "[":
                    ret += ", "
                error = l.split("critical : ")[1].replace("\n", '')
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
    def _parse_position(self, p):
        self.log("Parsing %s" % p)

        start_stop = p.replace("<position: ", '').replace("/>", "").split(" duration: ")

        if len(start_stop) < 2:
            self.warning("Got a unparsable value: %s" % p)
            return 0, 0

        if "speed:"in start_stop[1]:
            start_stop[1] = start_stop[1].split("speed:")[0].rstrip().lstrip()

        return utils.parse_gsttimeargs(start_stop[0]), utils.parse_gsttimeargs(start_stop[1])


    def _parse_buffering(self, b):
        return b.split("buffering... ")[1].split("%")[0], 100


    def _get_position(self):
        position = duration = -1

        self.debug("Getting position")
        self.reporter.out.seek(0)
        m = None
        for l in reversed(self.reporter.out.readlines()):
            l = l.lower()
            if "<position:" in l or "buffering" in l:
                m = l
                break

        if m is None:
            self.debug("Could not fine any positionning info")
            return position, duration

        for j in m.split("\r"):
            j = j.lstrip().rstrip()
            if j.startswith("<position:") and j.endswith("/>"):
                position, duration = self._parse_position(j)
            elif j.startswith("buffering") and j.endswith("%"):
                position, duration = self._parse_buffering(j)
            else:
                self.debug("No info in %s" % j)

        return position, duration

    def _get_last_seek_values(self):
        m = None
        rate = start = stop = None

        for l in reversed(self.reporter.out.readlines()):
            l = l.lower()
            if "seeking to: " in l:
                m = l
                break

        if m is None:
            self.debug("Could not fine any seeking info")
            return start, stop, rate

        tmp = m.split("seeking to: ")[1].split(" stop: ")
        start = tmp[0]
        stop_rate = tmp[1].split("  Rate")

        return utils.parse_gsttimeargs(start), \
            utils.parse_gsttimeargs(stop_rate[0]), float(stop_rate[1].replace(":",""))

    def get_current_position(self):
        position, duration = self._get_position()

        start, stop, rate = self._get_last_seek_values()
        if start and not (start - self.max_outside_segment * GST_SECOND < position < stop +
                          self.max_outside_segment):
            self.set_result(Result.FAILED,
                            "Position not in expected 'segment' (with %d second tolerance)"
                            "seek.start %d < position %d < seek.stop %d is FALSE"
                            % (self.max_outside_segment,
                               start - self.max_outside_segment, position,
                               stop + self.max_outside_segment)
                            )

        return position


    def get_current_size(self):
        position = self.get_current_position()

        size = os.stat(urlparse.urlparse(self.dest_file).path).st_size
        self.debug("Size: %s" % size)
        return size


class TestsManager(Loggable):

    """ A class responsible for managing tests. """

    name = ""

    def __init__(self):

        Loggable.__init__(self)

        self.tests = set([])
        self.unwanted_tests = set([])
        self.options = None
        self.args = None
        self.reporter = None
        self.wanted_tests_patterns = []
        self.blacklisted_tests_patterns = []

    def init(self):
        return False

    def list_tests(self):
        pass

    def add_test(self, test):
        if self._is_test_wanted(test):
            self.tests.add(test)
        else:
            self.unwanted_tests.add(test)

    def get_tests(self):
        return self.tests

    def get_blacklisted(self):
        return []

    def add_options(self, parser):
        """ Add more arguments. """
        pass

    def set_settings(self, options, args, reporter):
        """ Set properties after options parsing. """
        self.options = options
        self.args = args
        self.reporter = reporter

        if options.wanted_tests:
            for patterns in options.wanted_tests:
                for pattern in patterns.split(","):
                    self.wanted_tests_patterns.append(re.compile(pattern))

        if options.blacklisted_tests:
            for patterns in options.blacklisted_tests:
                for pattern in patterns.split(","):
                    self.blacklisted_tests_patterns.append(re.compile(pattern))

    def _check_blacklisted(self, test):
        for pattern in self.blacklisted_tests_patterns:
            if pattern.findall(test.classname):
                return True

        return False

    def _is_test_wanted(self, test):
        if self._check_blacklisted(test):
            return False

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
                res = test.run()
                self.reporter.after_test()
                if res != Result.PASSED and (self.options.forever or
                                             self.options.fatal_error):
                    return test.result

        return Result.PASSED

    def needs_http_server(self):
        return False


class _TestsLauncher(Loggable):
    def __init__(self):

        Loggable.__init__(self)

        self.options = None
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

        self.options = options
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

    def _run_tests(self):
        for tester in self.testers:
            res = tester.run_tests()
            if res != Result.PASSED and (self.options.forever or
                    self.options.fatal_error):
                return False

        return True

    def run_tests(self):
        if self.options.forever:
            while self._run_tests():
                continue

            return False
        else:
            return self._run_tests()

    def final_report(self):
        self.reporter.final_report()

    def needs_http_server(self):
        for tester in self.testers:
            if tester.needs_http_server():
                return True

    def get_blacklisted(self):
        res = []
        for tester in self.testers:
            for blacklisted in tester.get_blacklisted():
                if isinstance(blacklisted, str):
                    res.append(blacklisted, "Unknown")
                else:
                    res.append(blacklisted)
        return res


class NamedDic(object):

    def __init__(self, props):
        if props:
            for name, value in props.iteritems():
                setattr(self, name, value)


class Scenario(NamedDic):

    def __init__(self, name, props=None):
        self.name = name
        NamedDic.__init__(self, props)

    @classmethod
    def get_scenario(cls, name):
        return [scenario for scenario in ALL_SCENARIOS if scenario.name == name][0]

ALL_SCENARIOS = [
    Scenario("play_15s", {"max_duration": 15}),
    Scenario("simple_backward"),
    Scenario("fast_forward"),
    Scenario("seek_forward"),
    Scenario("seek_backward"),
    Scenario("scrub_forward_seeking"),
    Scenario("seek_with_stop"),
]
