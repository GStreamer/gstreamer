#!/usr/bin/env python2
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
import sys
import re
import time
import utils
import signal
import urlparse
import subprocess
import reporters
import ConfigParser
from loggable import Loggable
from optparse import OptionGroup

from utils import mkdir, Result, Colors, printc, DEFAULT_TIMEOUT, GST_SECOND


class Test(Loggable):

    """ A class representing a particular test. """

    def __init__(self, application_name, classname, options,
                 reporter, duration=0, timeout=DEFAULT_TIMEOUT,
                 hard_timeout=None):
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
        self.duration = duration

        self.clean()

    def clean(self):
        self.message = ""
        self.error_str = ""
        self.time_taken = 0.0
        self._starting_time = None
        self.result = Result.NOT_RUN
        self.logfile = None
        self.extra_logfiles = []

    def __str__(self):
        string = self.classname
        if self.result != Result.NOT_RUN:
            string += ": " + self.result
            if self.result in [Result.FAILED, Result.TIMEOUT]:
                string += " '%s'\n" \
                          "       You can reproduce with: %s\n" \
                          "       You can find logs in:\n" \
                          "             - %s" % (self.message, self.command,
                                                 self.logfile)
                for log in self.extra_logfiles:
                    string += "\n             - %s" % log

        return string

    def get_extra_log_content(self, extralog):
        if extralog not in self.extra_logfiles:
            return ""

        f = open(extralog, 'r+')
        value = f.read()
        f.close()

        return value


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
        self.debug("Setting result: %s (message: %s, error: %s", result,
                   message, error)
        if result is Result.TIMEOUT and self.options.debug is True:
            pname = subprocess.check_output(("readlink -e /proc/%s/exe"
                                             % self.process.pid).split(' ')).replace('\n', '')
            raw_input("%sTimeout happened you can attach gdb doing: $gdb %s %d%s\n"
                      "Press enter to continue" %(Colors.FAIL, pname, self.process.pid,
                                                   Colors.ENDC))

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
            self.set_result(Result.PASSED)
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
                    self.set_result(Result.TIMEOUT)
                    break
                continue
            elif val is Result.FAILED:
                break
            elif val is Result.KNOWN_ERROR:
                break

            self.log("New val %s" % val)

            if val == last_val:
                delta = time.time() - last_change_ts
                self.debug("%s: Same value for %d/%d seconds" % (self, delta, self.timeout))
                if delta > self.timeout:
                    self.set_result(Result.TIMEOUT)
                    break
            elif self.hard_timeout and time.time() - start_ts > self.hard_timeout:
                self.set_result(Result.TIMEOUT, "Hard timeout reached: %d", self.hard_timeout)
                break
            else:
                last_change_ts = time.time()
                last_val = val

        self.check_results()

    def get_subproc_env(self):
        return os.environ

    def run(self):
        self.command = "%s " % (self.application)
        self._starting_time = time.time()
        self.build_arguments()
        proc_env = self.get_subproc_env()

        message = "Launching: %s%s\n" \
                  "    Command: '%s'\n" \
                  "    Logs:\n" \
                  "         - %s" % (Colors.ENDC, self.classname,
                  self.command, self.logfile)
        for log in self.extra_logfiles:
            message += "\n         - %s" % log

        printc(message, Colors.OKBLUE)

        try:
            self.process = subprocess.Popen("exec " + self.command,
                                            stderr=self.reporter.out,
                                            stdout=self.reporter.out,
                                            shell=True,
                                            env=proc_env)
            self.wait_process()
        except KeyboardInterrupt:
            self.process.send_signal(signal.SIGINT)
            raise

        try:
            self.process.send_signal(signal.SIGINT)
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
    findpos_regex = re.compile('.*position.*(\d+):(\d+):(\d+).(\d+).*duration.*(\d+):(\d+):(\d+).(\d+)')
    findlastseek_regex = re.compile('seeking to.*(\d+):(\d+):(\d+).(\d+).*stop.*(\d+):(\d+):(\d+).(\d+).*rate.*(\d+)\.(\d+)')

    def __init__(self, application_name, classname,
                 options, reporter, duration=0,
                 timeout=DEFAULT_TIMEOUT, scenario=None, hard_timeout=None):

        super(GstValidateTest, self).__init__(application_name, classname, options,
                                              reporter, duration=duration,
                                              timeout=timeout, hard_timeout=hard_timeout)

        # defines how much the process can be outside of the configured
        # segment / seek
        self._sent_eos_pos = None

        self.validatelogs = None
        if scenario is None or scenario.name.lower() == "none":
            self.scenario = None
        else:
            self.scenario = scenario

    def get_subproc_env(self):
        subproc_env = os.environ.copy()

        self.validatelogs = self.logfile + '.validate.logs'
        utils.touch(self.validatelogs)
        subproc_env["GST_VALIDATE_FILE"] = self.validatelogs
        self.extra_logfiles.append(self.validatelogs)

        if 'GST_DEBUG' in os.environ:
            gstlogsfile = self.logfile + '.gstdebug'
            self.extra_logfiles.append(gstlogsfile)
            subproc_env["GST_DEBUG_FILE"] = gstlogsfile

        return subproc_env

    def clean(self):
        Test.clean(self)
        self._sent_eos_pos = None

    def build_arguments(self):
        if self.scenario is not None:
            self.add_arguments("--set-scenario",
                               self.scenario.get_execution_name())

    def get_extra_log_content(self, extralog):
        value = Test.get_extra_log_content(self, extralog)

        if extralog == self.validatelogs:
            value = re.sub("<position:.*/>\r", "", value)

        return value

    def get_validate_criticals_errors(self):
        ret = "["
        errors = []
        for l in open(self.validatelogs, 'r').readlines():
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
        if self.result is Result.FAILED or self.result is Result.PASSED:
            return

        self.debug("%s returncode: %s", self, self.process.returncode)
        if self.result == Result.TIMEOUT:
            self.set_result(Result.TIMEOUT, "Application timed out", "timeout")
        elif self.process.returncode == 0:
            self.set_result(Result.PASSED)
        else:
            if self.process.returncode == 139:
                # FIXME Reimplement something like that if needed
                # self.get_backtrace("SEGFAULT")
                self.set_result(Result.FAILED,
                                "Application segfaulted",
                                "segfault")
            else:
                self.set_result(Result.FAILED,
                                "Application returned %s (issues: %s)" % (
                                self.process.returncode,
                                self.get_validate_criticals_errors()
                                ))
    def _parse_position(self, p):
        self.log("Parsing %s" % p)
        times = self.findpos_regex.findall(p)

        if len(times) != 1:
            self.warning("Got a unparsable value: %s" % p)
            return 0, 0

        return (utils.gsttime_from_tuple(times[0][:4]),
                utils.gsttime_from_tuple(times[0][4:]))


    def _parse_buffering(self, b):
        return b.split("buffering... ")[1].split("%")[0], 100


    def _get_position(self):
        position = duration = -1

        self.debug("Getting position")
        m = None
        for l in reversed(open(self.validatelogs, 'r').readlines()):
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
                self.log("No info in %s" % j)

        return position, duration

    def _get_last_seek_values(self):
        m = None
        rate = start = stop = None

        for l in reversed(open(self.validatelogs, 'r').readlines()):
            l = l.lower()
            if "seeking to: " in l:
                m = l
                break

        if m is None:
            self.debug("Could not fine any seeking info")
            return start, stop, rate


        values = self.findlastseek_regex.findall(m)
        if len(values) != 1:
            self.warning("Got a unparsable value: %s" % p)
            return start, stop, rate

        v = values[0]
        return (utils.gsttime_from_tuple(v[:4]),
                utils.gsttime_from_tuple(v[4:8]),
                float(str(v[8]) + "." + str(v[9])))

    def sent_eos_position(self):
        if self._sent_eos_pos is not None:
            return self._sent_eos_pos

        m = None
        rate = start = stop = None

        for l in reversed(open(self.validatelogs, 'r').readlines()):
            l = l.lower()
            if "sending eos" in l:
                m = l
                self._sent_eos_pos = time.time()
                return self._sent_eos_pos

        return None

    def get_current_position(self):
        position, duration = self._get_position()
        if position == -1:
            return position

        return position


    def get_current_size(self):
        position = self.get_current_position()

        try:
            size = os.stat(urlparse.urlparse(self.dest_file).path).st_size
        except OSError as e:
            return position

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
        self._generators = []

    def init(self):
        return False

    def list_tests(self):
        return self.tests

    def add_test(self, test):
        if self._is_test_wanted(test):
            self.tests.add(test)
        else:
            self.unwanted_tests.add(test)

    def get_tests(self):
        return self.tests

    def populate_testsuite(self):
        pass

    def add_generators(self, generators):
        """
        @generators: A list of, or one single #TestsGenerator to be used to generate tests
        """
        if isinstance(generators, list):
            self._generators.extend(generators)
        else:
            self._generators.append(generators)

    def get_generators(self):
        return self._generators

    def set_default_blacklist(self, default_blacklist):
        msg = "\nCurrently 'hardcoded' %s blacklisted tests:\n\n" % self.name
        for name, bug in default_blacklist:
            self.options.blacklisted_tests.append(name)
            msg += "  + %s \n   --> bug: %s\n" % (name, bug)

        printc(msg, Colors.FAIL, True)


    def add_options(self, parser):
        """ Add more arguments. """
        pass

    def set_settings(self, options, args, reporter):
        """ Set properties after options parsing. """
        self.options = options
        self.args = args
        self.reporter = reporter

        self.populate_testsuite()
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

        if test.duration > 0 and int(self.options.long_limit) < int(test.duration):
            self.info("Not activating test as it duration (%d) is superior"
                      " than the long limit (%d)" % (test.duration,
                                                     int(self.options.long_limit)))
            return False


        if not self.wanted_tests_patterns:
            return True

        for pattern in self.wanted_tests_patterns:
            if pattern.findall(test.classname):
                return True

        return False

    def run_tests(self, cur_test_num, total_num_tests):
        i = cur_test_num
        for test in self.tests:
            sys.stdout.write("[%d / %d] " % (i, total_num_tests))
            self.reporter.before_test(test)
            res = test.run()
            i += 1
            self.reporter.after_test()
            if res != Result.PASSED and (self.options.forever or
                                         self.options.fatal_error):
                return test.result

        return Result.PASSED

    def clean_tests(self):
        for test in self.tests:
            test.clean()

    def needs_http_server(self):
        return False


class TestsGenerator(Loggable):
    def __init__(self, name, test_manager, tests=[]):
        Loggable.__init__(self)
        self.name = name
        self.test_manager = test_manager
        self._tests = {}
        for test in tests:
            self._tests[test.classname] = test

    def generate_tests(self, *kwargs):
        """
        Method that generates tests
        """
        return list(self._tests.values())

    def add_test(self, test):
        self._tests[test.classname] = test


class GstValidateTestsGenerator(TestsGenerator):
    def populate_tests(self, uri_minfo_special_scenarios, scenarios):
        pass

    def generate_tests(self, uri_minfo_special_scenarios, scenarios):
        self.populate_tests(uri_minfo_special_scenarios, scenarios)
        return super(GstValidateTestsGenerator, self).generate_tests()


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
        env = globals().copy()
        d = os.path.dirname(__file__)
        for f in os.listdir(os.path.join(d, "apps")):
            if f.endswith(".py"):
                execfile(os.path.join(d, "apps", f), env)

        testers = [i() for i in utils.get_subclasses(TestsManager, env)]
        for tester in testers:
            if tester.init() is True:
                self.testers.append(tester)
            else:
                self.warning("Can not init tester: %s -- PATH is %s"
                             % (tester.name, os.environ["PATH"]))

    def add_options(self, parser):
        for tester in self.testers:
            tester.add_options(parser)

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

        if options.config:
            for tester in self.testers:
                tester.options = options
                globals()[tester.name] = tester
            globals()["options"] = options
            execfile(self.options.config, globals())

        for tester in self.testers:
            tester.set_settings(options, args, self.reporter)


    def list_tests(self):
        for tester in self.testers:
            tester.list_tests()
            self.tests.extend(tester.tests)
        return self.tests

    def _run_tests(self):
        cur_test_num = 0
        total_num_tests = 0
        for tester in self.testers:
            total_num_tests += len(tester.list_tests())

        for tester in self.testers:
            res = tester.run_tests(cur_test_num, total_num_tests)
            cur_test_num += len(tester.list_tests())
            if res != Result.PASSED and (self.options.forever or
                    self.options.fatal_error):
                return False

        return True

    def _clean_tests(self):
        for tester in self.testers:
            tester.clean_tests()

    def run_tests(self):
        if self.options.forever:
            while self._run_tests():
                self._clean_tests()

            return False
        else:
            return self._run_tests()

    def final_report(self):
        self.reporter.final_report()

    def needs_http_server(self):
        for tester in self.testers:
            if tester.needs_http_server():
                return True


class NamedDic(object):

    def __init__(self, props):
        if props:
            for name, value in props.iteritems():
                setattr(self, name, value)

class Scenario(object):
    def __init__(self, name, props, path=None):
        self.name = name
        self.path = path

        for prop, value in props:
            setattr(self, prop.replace("-", "_"), value)

    def get_execution_name(self):
        if self.path is not None:
            return self.path
        else:
            return self.name

    def seeks(self):
        if hasattr(self, "seek"):
            return bool(self.seek)

        return False

    def does_reverse_playback(self):
        if hasattr(self, "reverse_playback"):
            return bool(self.seek)

        return False

    def get_duration(self):
        try:
            return float(getattr(self, "duration"))
        except AttributeError:
            return 0

    def get_min_tracks(self, track_type):
        try:
            return int(getattr(self, "min_%s_track" % track_type))
        except AttributeError:
            return 0


class ScenarioManager(Loggable):
    _instance = None
    all_scenarios = []

    FILE_EXTENDION = "scenario"
    GST_VALIDATE_COMMAND = "gst-validate-1.0"
    if "win32" in sys.platform:
        GST_VALIDATE_COMMAND += ".exe"

    def __new__(cls, *args, **kwargs):
        if not cls._instance:
            cls._instance = super(ScenarioManager, cls).__new__(
                                cls, *args, **kwargs)
            cls._instance.config = None
            cls._instance.discovered = False
            Loggable.__init__(cls._instance)

        return cls._instance

    def find_special_scenarios(self, mfile):
        scenarios = []
        mfile_bname = os.path.basename(mfile)
        for f in os.listdir(os.path.dirname(mfile)):
            if re.findall("%s\..*\.%s$" % (mfile_bname, self.FILE_EXTENDION),
                          f):
                scenarios.append(os.path.join(os.path.dirname(mfile), f))

        if scenarios:
            scenarios = self.discover_scenarios(scenarios, mfile)


        return scenarios

    def discover_scenarios(self, scenario_paths=[], mfile=None):
        """
        Discover scenarios specified in scenario_paths or the default ones
        if nothing specified there
        """
        scenarios = []
        scenario_defs = os.path.join(self.config.main_dir, "scenarios.def")
        logs = open(os.path.join(self.config.logsdir, "scenarios_discovery.log"), 'w')
        try:
            command = [self.GST_VALIDATE_COMMAND, "--scenarios-defs-output-file", scenario_defs]
            command.extend(scenario_paths)
            subprocess.check_call(command, stdout=logs, stderr=logs)
        except subprocess.CalledProcessError:
            pass

        config = ConfigParser.ConfigParser()
        f = open(scenario_defs)
        config.readfp(f)

        for section in config.sections():
            if scenario_paths:
                for scenario_path in scenario_paths:
                    if section in scenario_path:
                        # The real name of the scenario is:
                        # filename.REALNAME.scenario
                        name = scenario_path.replace(mfile + ".", "").replace("." + self.FILE_EXTENDION, "")
                        path = scenario_path
            else:
                name = section
                path = None

            scenarios.append(Scenario(name, config.items(section), path))

        if not scenario_paths:
            self.discovered = True
            self.all_scenarios.extend(scenarios)

        return scenarios

    def get_scenario(self, name):
        if self.discovered is False:
            self.discover_scenarios()

        if name is None:
            return self.all_scenarios

        try:
            return [scenario for scenario in self.all_scenarios if scenario.name == name][0]
        except IndexError:
            self.warning("Scenario: %s not found" % name)
            return None


class GstValidateBaseTestManager(TestsManager):
    scenarios_manager = ScenarioManager()

    def __init__(self):
        super(GstValidateBaseTestManager, self).__init__()
        self._scenarios = []
        self._encoding_formats = []

    def add_scenarios(self, scenarios):
        """
        @scenarios: A list or a unic scenario name(s) to be run on the tests.
                    They are just the default scenarios, and then depending on
                    the TestsGenerator to be used you can have more fine grained
                    control on what to be run on each serie of tests.
        """
        if isinstance(scenarios, list):
            self._scenarios.extend(scenarios)
        else:
            self._scenarios.append(scenarios)

    def get_scenarios(self):
        return self._scenarios


    def add_encoding_formats(self, encoding_formats):
        """
        @encoding_formats: A list or one single #MediaFormatCombinations describing wanted output
                           formats for transcoding test.
                           They are just the default encoding formats, and then depending on
                           the TestsGenerator to be used you can have more fine grained
                           control on what to be run on each serie of tests.
        """
        if isinstance(encoding_formats, list):
            self._encoding_formats.extend(encoding_formats)
        else:
            self._encoding_formats.append(encoding_formats)

    def get_encoding_formats(self):
        return self._encoding_formats
