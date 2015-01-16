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
import imp
import re
import time
import utils
import signal
import urlparse
import subprocess
import threading
import Queue
import reporters
import ConfigParser
import loggable
from loggable import Loggable
import xml.etree.ElementTree as ET

from utils import mkdir, Result, Colors, printc, DEFAULT_TIMEOUT, GST_SECOND, \
    Protocols


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
        self.proc_env = None
        self.thread = None
        self.queue = Queue.Queue()
        self.duration = duration

        self.clean()

    def clean(self):
        self.message = ""
        self.error_str = ""
        self.time_taken = 0.0
        self._starting_time = None
        self.result = Result.NOT_RUN
        self.logfile = None
        self.out = None
        self.extra_logfiles = []
        self._env_variable = ''

    def __str__(self):
        string = self.classname
        if self.result != Result.NOT_RUN:
            string += ": " + self.result
            if self.result in [Result.FAILED, Result.TIMEOUT]:
                string += " '%s'\n" \
                          "       You can reproduce with: %s %s\n" \
                    % (self.message, self._env_variable, self.command)

                if not self.options.redirect_logs:
                    string += "       You can find logs in:\n" \
                              "             - %s" % (self.logfile)
                for log in self.extra_logfiles:
                    string += "\n             - %s" % log

        return string

    def add_env_variable(self, variable, value):
        """
        Only usefull so that the gst-validate-launcher can print the exact
        right command line to reproduce the tests
        """
        if self._env_variable:
            self._env_variable += " "
        self._env_variable += "%s=%s" % (variable, value)

    def open_logfile(self):
        path = os.path.join(self.options.logsdir,
                            self.classname.replace(".", os.sep))
        mkdir(os.path.dirname(path))
        self.logfile = path

        if self.options.redirect_logs == 'stdout':
            self.out = sys.stdout
        elif self.options.redirect_logs == 'stderr':
            self.out = sys.stderr
        else:
            self.out = open(path, 'w+')

    def close_logfile(self):
        if not self.options.redirect_logs:
            self.out.close()

        self.out = None

    def _get_file_content(self, file_name):
        f = open(file_name, 'r+')
        value = f.read()
        f.close()

        return value

    def get_log_content(self):
        return self._get_file_content(self.logfile)

    def get_extra_log_content(self, extralog):
        if extralog not in self.extra_logfiles:
            return ""

        return self._get_file_content(extralog)

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
        self.debug("Setting result: %s (message: %s, error: %s)" % (result,
                   message, error))
        if result is Result.TIMEOUT and self.options.debug is True:
            pname = subprocess.check_output(("readlink -e /proc/%s/exe"
                                             % self.process.pid).split(' ')).replace('\n', '')
            raw_input("%sTimeout happened you can attach gdb doing: $gdb %s %d%s\n"
                      "Press enter to continue" % (Colors.FAIL, pname, self.process.pid,
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
        while True:
            # Check process every second for timeout
            try:
                self.queue.get(timeout=1)
            except Queue.Empty:
                pass

            if self.process_update():
                break

        self.check_results()

    def process_update(self):
        """
        Returns True when process has finished running or has timed out.
        """
        self.process.poll()
        if self.process.returncode is not None:
            return True

        val = self.get_current_value()

        self.debug("Got value: %s" % val)
        if val is Result.NOT_RUN:
            # The get_current_value logic is not implemented... dumb
            # timeout
            if time.time() - self.last_change_ts > self.timeout:
                self.set_result(Result.TIMEOUT)
                return True
            return False
        elif val is Result.FAILED:
            return True
        elif val is Result.KNOWN_ERROR:
            return True

        self.log("New val %s" % val)

        if val == self.last_val:
            delta = time.time() - self.last_change_ts
            self.debug("%s: Same value for %d/%d seconds" %
                       (self, delta, self.timeout))
            if delta > self.timeout:
                self.set_result(Result.TIMEOUT)
                return True
        elif self.hard_timeout and time.time() - self.start_ts > self.hard_timeout:
            self.set_result(
                Result.TIMEOUT, "Hard timeout reached: %d secs" % self.hard_timeout)
            return True
        else:
            self.last_change_ts = time.time()
            self.last_val = val

        return False

    def get_subproc_env(self):
        return os.environ

    def _kill_subprocess(self):
        if self.process is None:
            return

        stime = time.time()
        res = self.process.poll()
        while res is None:
            try:
                self.debug("Subprocess is still alive, sending KILL signal")
                self.process.send_signal(signal.SIGKILL)
                time.sleep(1)
            except OSError:
                pass
            if time.time() - stime > DEFAULT_TIMEOUT:
                raise RuntimeError("Could not kill subprocess after %s second"
                                   " Something is really wrong, => EXITING"
                                   % DEFAULT_TIMEOUT)
            res = self.process.poll()

    def thread_wrapper(self):
        self.process = subprocess.Popen("exec " + self.command,
                                        stderr=self.out,
                                        stdout=self.out,
                                        shell=True,
                                        env=self.proc_env)
        self.process.wait()
        if self.result is not Result.TIMEOUT:
            self.queue.put(None)

    def run(self):
        self.open_logfile()

        self.command = "%s " % (self.application)
        self._starting_time = time.time()
        self.build_arguments()
        self.proc_env = self.get_subproc_env()

        message = "Launching: %s%s\n" \
                  "    Command: '%s %s'\n" % (Colors.ENDC, self.classname,
                                              self._env_variable, self.command)
        if not self.options.redirect_logs:
            message += "    Logs:\n" \
                       "         - %s" % (self.logfile)
            for log in self.extra_logfiles:
                message += "\n         - %s" % log

            self.out.write("=================\n"
                           "Test name: %s\n"
                           "Command: '%s'\n"
                           "=================\n\n"
                           % (self.classname, self.command))
            self.out.flush()

        printc(message, Colors.OKBLUE)

        self.thread = threading.Thread(target=self.thread_wrapper)
        self.thread.start()

        self.last_val = 0
        self.last_change_ts = time.time()
        self.start_ts = time.time()

        try:
            self.wait_process()
        except KeyboardInterrupt:
            self._kill_subprocess()
            raise

        self._kill_subprocess()
        self.thread.join()
        self.time_taken = time.time() - self._starting_time

        printc("Result: %s%s\n" % (self.result,
               " (" + self.message + ")" if self.message else ""),
               color=utils.get_color_for_result(self.result))

        self.close_logfile()

        return self.result


class GstValidateTest(Test):

    """ A class representing a particular test. """
    findpos_regex = re.compile(
        '.*position.*(\d+):(\d+):(\d+).(\d+).*duration.*(\d+):(\d+):(\d+).(\d+)')
    findlastseek_regex = re.compile(
        'seeking to.*(\d+):(\d+):(\d+).(\d+).*stop.*(\d+):(\d+):(\d+).(\d+).*rate.*(\d+)\.(\d+)')

    HARD_TIMEOUT_FACTOR = 5

    def __init__(self, application_name, classname,
                 options, reporter, duration=0,
                 timeout=DEFAULT_TIMEOUT, scenario=None, hard_timeout=None):

        if not hard_timeout and self.HARD_TIMEOUT_FACTOR:
            if timeout:
                hard_timeout = timeout * self.HARD_TIMEOUT_FACTOR
            elif duration:
                hard_timeout = duration * self.HARD_TIMEOUT_FACTOR
            else:
                hard_timeout = None

        super(GstValidateTest, self).__init__(application_name, classname,
                                              options, reporter,
                                              duration=duration,
                                              timeout=timeout,
                                              hard_timeout=hard_timeout)

        # defines how much the process can be outside of the configured
        # segment / seek
        self._sent_eos_pos = None

        self.validatelogs = None
        if scenario is None or scenario.name.lower() == "none":
            self.scenario = None
        else:
            self.scenario = scenario

    def get_subproc_env(self):
        self.validatelogs = self.logfile + '.validate.logs'
        logfiles = self.validatelogs
        if self.options.redirect_logs:
            logfiles += os.pathsep + \
                self.options.redirect_logs.replace("<", '').replace(">", '')

        subproc_env = os.environ.copy()

        utils.touch(self.validatelogs)
        subproc_env["GST_VALIDATE_FILE"] = logfiles
        self.extra_logfiles.append(self.validatelogs)

        if 'GST_DEBUG' in os.environ:
            gstlogsfile = self.logfile + '.gstdebug'
            self.extra_logfiles.append(gstlogsfile)
            subproc_env["GST_DEBUG_FILE"] = gstlogsfile
        elif self.options.no_color:
            subproc_env["GST_DEBUG_NO_COLOR"] = '1'

        return subproc_env

    def clean(self):
        Test.clean(self)
        self._sent_eos_pos = None

    def build_arguments(self):
        if "GST_VALIDATE" in os.environ:
            self.add_env_variable("GST_VALIDATE", os.environ["GST_VALIDATE"])

        if "GST_VALIDATE_SCENARIOS_PATH" in os.environ:
            self.add_env_variable("GST_VALIDATE_SCENARIOS_PATH",
                                  os.environ["GST_VALIDATE_SCENARIOS_PATH"])
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
                error = l.split("critical : ")[1].replace("\n", '')
                if error not in errors:
                    if ret != "[":
                        ret += ", "
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
            self.warning("Got an unparsable seek value %s", m)
            return start, stop, rate

        v = values[0]
        return (utils.gsttime_from_tuple(v[:4]),
                utils.gsttime_from_tuple(v[4:8]),
                float(str(v[8]) + "." + str(v[9])))

    def sent_eos_position(self):
        if self._sent_eos_pos is not None:
            return self._sent_eos_pos

        for l in reversed(open(self.validatelogs, 'r').readlines()):
            l = l.lower()
            if "sending eos" in l:
                self._sent_eos_pos = time.time()
                return self._sent_eos_pos

        return None

    def get_current_position(self):
        position, duration = self._get_position()
        if position == -1:
            return position

        return position


class GstValidateEncodingTestInterface(object):
    DURATION_TOLERANCE = GST_SECOND / 4

    def __init__(self, combination, media_descriptor, duration_tolerance=None):
        super(GstValidateEncodingTestInterface, self).__init__()

        self.media_descriptor = media_descriptor
        self.combination = combination
        self.dest_file = ""

        self._duration_tolerance = duration_tolerance
        if duration_tolerance is None:
            self._duration_tolerance = self.DURATION_TOLERANCE

    def get_current_size(self):
        try:
            size = os.stat(urlparse.urlparse(self.dest_file).path).st_size
        except OSError:
            return None

        self.debug("Size: %s" % size)
        return size

    def _get_profile_full(self, muxer, venc, aenc, video_restriction=None,
                          audio_restriction=None, audio_presence=0,
                          video_presence=0):
        ret = "\""
        if muxer:
            ret += muxer
        ret += ":"
        if venc:
            if video_restriction is not None:
                ret = ret + video_restriction + '->'
            ret += venc
            if video_presence:
                ret = ret + '|' + str(video_presence)
        if aenc:
            ret += ":"
            if audio_restriction is not None:
                ret = ret + audio_restriction + '->'
            ret += aenc
            if audio_presence:
                ret = ret + '|' + str(audio_presence)

        ret += "\""
        return ret.replace("::", ":")

    def get_profile(self, video_restriction=None, audio_restriction=None):
        vcaps = self.combination.get_video_caps()
        acaps = self.combination.get_audio_caps()
        if self.media_descriptor is not None:
            if self.media_descriptor.get_num_tracks("video") == 0:
                vcaps = None

            if self.media_descriptor.get_num_tracks("audio") == 0:
                acaps = None

        return self._get_profile_full(self.combination.get_muxer_caps(),
                                      vcaps, acaps,
                                      video_restriction=video_restriction,
                                      audio_restriction=audio_restriction)

    def _clean_caps(self, caps):
        """
        Returns a list of key=value or structure name, without "(types)" or ";" or ","
        """
        return re.sub(r"\(.+?\)\s*| |;", '', caps).split(',')

    def _has_caps_type_variant(self, c, ccaps):
        """
        Handle situations where we can have application/ogg or video/ogg or
        audio/ogg
        """
        has_variant = False
        media_type = re.findall("application/|video/|audio/", c)
        if media_type:
            media_type = media_type[0].replace('/', '')
            possible_mtypes = ["application", "video", "audio"]
            possible_mtypes.remove(media_type)
            for tmptype in possible_mtypes:
                possible_c_variant = c.replace(media_type, tmptype)
                if possible_c_variant in ccaps:
                    self.info(
                        "Found %s in %s, good enough!", possible_c_variant)
                    has_variant = True

        return has_variant

    def check_encoded_file(self):
        result_descriptor = GstValidateMediaDescriptor.new_from_uri(
            self.dest_file)
        if result_descriptor is None:
            return (Result.FAILED, "Could not discover encoded file %s"
                    % self.dest_file)

        duration = result_descriptor.get_duration()
        orig_duration = self.media_descriptor.get_duration()
        tolerance = self._duration_tolerance

        if orig_duration - tolerance >= duration <= orig_duration + tolerance:
            os.remove(result_descriptor.get_path())
            return (Result.FAILED, "Duration of encoded file is "
                    " wrong (%s instead of %s)" %
                    (utils.TIME_ARGS(duration),
                     utils.TIME_ARGS(orig_duration)))
        else:
            all_tracks_caps = result_descriptor.get_tracks_caps()
            container_caps = result_descriptor.get_caps()
            if container_caps:
                all_tracks_caps.insert(0, ("container", container_caps))

            for track_type, caps in all_tracks_caps:
                ccaps = self._clean_caps(caps)
                wanted_caps = self.combination.get_caps(track_type)
                cwanted_caps = self._clean_caps(wanted_caps)

                if wanted_caps is None:
                    os.remove(result_descriptor.get_path())
                    return (Result.FAILED,
                            "Found a track of type %s in the encoded files"
                            " but none where wanted in the encoded profile: %s"
                            % (track_type, self.combination))

                for c in cwanted_caps:
                    if c not in ccaps:
                        if not self._has_caps_type_variant(c, ccaps):
                            os.remove(result_descriptor.get_path())
                            return (Result.FAILED,
                                    "Field: %s  (from %s) not in caps of the outputed file %s"
                                    % (wanted_caps, c, ccaps))

            os.remove(result_descriptor.get_path())
            return (Result.PASSED, "")


class TestsManager(Loggable):

    """ A class responsible for managing tests. """

    name = ""

    def __init__(self):

        Loggable.__init__(self)

        self.tests = []
        self.unwanted_tests = []
        self.options = None
        self.args = None
        self.reporter = None
        self.wanted_tests_patterns = []
        self.blacklisted_tests_patterns = []
        self._generators = []

    def init(self):
        return False

    def list_tests(self):
        return sorted(list(self.tests))

    def add_test(self, test):
        if self._is_test_wanted(test):
            if test not in self.tests:
                self.tests.append(test)
                self.tests.sort(key=lambda test: test.classname)
        else:
            if test not in self.tests:
                self.unwanted_tests.append(test)
                self.unwanted_tests.sort(key=lambda test: test.classname)

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

        self._generators = list(set(self._generators))

    def get_generators(self):
        return self._generators

    def _add_blacklist(self, blacklisted_tests):
        if not isinstance(blacklisted_tests, list):
            blacklisted_tests = [blacklisted_tests]

        for patterns in blacklisted_tests:
            for pattern in patterns.split(","):
                self.blacklisted_tests_patterns.append(re.compile(pattern))

    def set_default_blacklist(self, default_blacklist):
        msg = "\nCurrently 'hardcoded' %s blacklisted tests:\n\n" % self.name
        for name, bug in default_blacklist:
            self._add_blacklist(name)
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
                self._add_blacklist(patterns)

    def _check_blacklisted(self, test):
        for pattern in self.blacklisted_tests_patterns:
            if pattern.findall(test.classname):
                return True

        return False

    def _is_test_wanted(self, test):
        if self._check_blacklisted(test):
            return False

        if test.duration > 0 and int(self.options.long_limit) < int(test.duration):
            self.info("Not activating %s as its duration (%d) is superior"
                      " than the long limit (%d)" % (test, test.duration,
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
            sys.stdout.write("[%d / %d] " % (i + 1, total_num_tests))
            res = test.run()
            i += 1
            self.reporter.after_test(test)
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

    def __init__(self, libsdir):

        Loggable.__init__(self)

        self.libsdir = libsdir
        self.options = None
        self.testers = []
        self.tests = []
        self.reporter = None
        self._list_testers()
        self.wanted_tests_patterns = []

    def _list_app_dirs(self):
        app_dirs = []
        app_dirs.append(os.path.join(self.libsdir, "apps"))
        env_dirs = os.environ.get("GST_VALIDATE_APPS_DIR")
        if env_dirs is not None:
            for dir_ in env_dirs.split(":"):
                app_dirs.append(dir_)

        return app_dirs

    def _exec_app(self, app_dir, env):
        try:
            files = os.listdir(app_dir)
        except OSError as e:
            self.debug("Could not list %s: %s" % (app_dir, e))
            files = []
        for f in files:
            if f.endswith(".py"):
                execfile(os.path.join(app_dir, f), env)

    def _exec_apps(self, env):
        app_dirs = self._list_app_dirs()
        for app_dir in app_dirs:
            self._exec_app(app_dir, env)

    def _list_testers(self):
        env = globals().copy()
        self._exec_apps(env)

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

    def _load_testsuites(self):
        testsuites = []
        for testsuite in self.options.testsuites:
            if not os.path.isabs(testsuite):
                testsuite = os.path.join(self.options.testsuites_dir, testsuite + ".py")

            try:
                sys.path.insert(0, os.path.dirname(testsuite))
                module = __import__(os.path.basename(testsuite).replace(".py", ""))
            except Exception as e:
                printc("Could not load testsuite: %s, reason: %s"
                       % (testsuite, e), Colors.FAIL)
                continue
            finally:
                sys.path.remove(os.path.dirname(testsuite))

            testsuites.append(module)
            if not hasattr(module, "TEST_MANAGER"):
                module.TEST_MANAGER = [tester.name for tester in self.testers]
            elif not isinstance(module.TEST_MANAGER, list):
                module.TEST_MANAGER = [module.TEST_MANAGER]

        self.options.testsuites = testsuites

    def _setup_testsuites(self):
        for testsuite in self.options.testsuites:
            loaded = False
            wanted_test_manager = None
            if hasattr(testsuite, "TEST_MANAGER"):
                wanted_test_manager = testsuite.TEST_MANAGER
                if not isinstance(wanted_test_manager, list):
                    wanted_test_manager = [wanted_test_manager]

            for tester in self.testers:
                if wanted_test_manager is not None and \
                        tester.name not in wanted_test_manager:
                    continue

                if testsuite.setup_tests(tester, self.options):
                    loaded = True

            if not loaded:
                printc("Could not load testsuite: %s"
                       " maybe because of missing TestManager"
                       % (testsuite), Colors.FAIL)

    def _load_config(self, options):
        printc("Loading config files is DEPRECATED"
               " you should use the new testsuite format now",)

        for tester in self.testers:
            tester.options = options
            globals()[tester.name] = tester
        globals()["options"] = options
        c__file__ = __file__
        globals()["__file__"] = self.options.config
        execfile(self.options.config, globals())
        globals()["__file__"] = c__file__

    def set_settings(self, options, args):
        self.reporter = reporters.XunitReporter(options)

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
            self._load_config(options)

        self._load_testsuites()

        for tester in self.testers:
            tester.set_settings(options, args, self.reporter)

        if not options.config and options.testsuites:
            self._setup_testsuites()

    def _other_testsuite_for_tester(self, testsuite, tester):
        if len(testsuite.TEST_MANAGER) > 1:
            return True

        if tester.name != testsuite.TEST_MANAGER[0]:
            return True

        for t in self.options.testsuites:
            if t != testsuite:
                for other_testmanager in testsuite.TEST_MANAGER:
                    if other_testmanager == tester.name:
                        return True

        return False

    def _check_defined_tests(self, tester, tests):
        if self.options.blacklisted_tests or self.options.wanted_tests:
            return

        tests_names = [test.classname for test in tests]
        for testsuite in self.options.testsuites:
            if not self._other_testsuite_for_tester(testsuite, tester):
                try:
                    testlist_file = open(os.path.splitext(testsuite.__file__)[0] + ".testslist",
                                    'rw')

                    know_tests = testlist_file.read().split("\n")
                    testlist_file.close()

                    testlist_file = open(os.path.splitext(testsuite.__file__)[0] + ".testslist",
                                    'w')
                except IOError:
                    return

                for test in know_tests:
                    if test and test not in tests_names:
                        printc("Test %s Not in testsuite %s anymore"
                               % (test, testsuite.__file__), Colors.FAIL)

                for test in tests_names:
                    testlist_file.write("%s\n" % test)
                    if test and test not in know_tests:
                        printc("Test %s is NEW in testsuite %s"
                               % (test, testsuite.__file__), Colors.OKGREEN)

                testlist_file.close()
                return

    def list_tests(self):
        for tester in self.testers:
            tests = tester.list_tests()
            self._check_defined_tests(tester, tests)
            self.tests.extend(tests)
        return sorted(list(self.tests))

    def _run_tests(self):
        cur_test_num = 0
        total_num_tests = 0
        for tester in self.testers:
            total_num_tests += len(tester.list_tests())

        self.reporter.init_timer()
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

    def needs_clock_sync(self):
        if hasattr(self, "need_clock_sync"):
            return bool(self.need_clock_sync)

        return False

    def get_min_media_duration(self):
        if hasattr(self, "min_media_duration"):
            return long(self.min_media_duration)

        return 0

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
        logs = open(os.path.join(self.config.logsdir,
                                 "scenarios_discovery.log"), 'w')

        try:
            command = [self.GST_VALIDATE_COMMAND,
                       "--scenarios-defs-output-file", scenario_defs]
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
                        name = scenario_path.replace(mfile + ".", "").replace(
                            "." + self.FILE_EXTENDION, "")
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
        @scenarios A list or a unic scenario name(s) to be run on the tests.
                    They are just the default scenarios, and then depending on
                    the TestsGenerator to be used you can have more fine grained
                    control on what to be run on each serie of tests.
        """
        if isinstance(scenarios, list):
            self._scenarios.extend(scenarios)
        else:
            self._scenarios.append(scenarios)

        self._scenarios = list(set(self._scenarios))

    def get_scenarios(self):
        return self._scenarios

    def add_encoding_formats(self, encoding_formats):
        """
        :param encoding_formats: A list or one single #MediaFormatCombinations describing wanted output
                           formats for transcoding test.
                           They are just the default encoding formats, and then depending on
                           the TestsGenerator to be used you can have more fine grained
                           control on what to be run on each serie of tests.
        """
        if isinstance(encoding_formats, list):
            self._encoding_formats.extend(encoding_formats)
        else:
            self._encoding_formats.append(encoding_formats)

        self._encoding_formats = list(set(self._encoding_formats))

    def get_encoding_formats(self):
        return self._encoding_formats


class MediaDescriptor(Loggable):

    def __init__(self):
        Loggable.__init__(self)

    def get_path(self):
        raise NotImplemented

    def get_media_filepath(self):
        raise NotImplemented

    def get_caps(self):
        raise NotImplemented

    def get_uri(self):
        raise NotImplemented

    def get_duration(self):
        raise NotImplemented

    def get_protocol(self):
        raise NotImplemented

    def is_seekable(self):
        raise NotImplemented

    def is_image(self):
        raise NotImplemented

    def get_num_tracks(self, track_type):
        raise NotImplemented

    def is_compatible(self, scenario):
        if scenario.seeks() and (not self.is_seekable() or self.is_image()):
            self.debug("Do not run %s as %s does not support seeking",
                       scenario, self.get_uri())
            return False

        if self.is_image() and scenario.needs_clock_sync():
            self.debug("Do not run %s as %s is an image",
                       scenario, self.get_uri())
            return False

        if self.get_duration() / GST_SECOND < scenario.get_min_media_duration():
            self.debug(
                "Do not run %s as %s is too short (%i < min media duation : %i",
                scenario, self.get_uri(),
                self.get_duration() / GST_SECOND,
                scenario.get_min_media_duration())
            return False

        for track_type in ['audio', 'subtitle']:
            if self.get_num_tracks(track_type) < scenario.get_min_tracks(track_type):
                self.debug("%s -- %s | At least %s %s track needed  < %s"
                           % (scenario, self.get_uri(), track_type,
                              scenario.get_min_tracks(track_type),
                              self.get_num_tracks(track_type)))
                return False

        return True


class GstValidateMediaDescriptor(MediaDescriptor):
    # Some extension file for discovering results
    MEDIA_INFO_EXT = "media_info"
    STREAM_INFO_EXT = "stream_info"

    DISCOVERER_COMMAND = "gst-validate-media-check-1.0"
    if "win32" in sys.platform:
        DISCOVERER_COMMAND += ".exe"

    def __init__(self, xml_path):
        super(GstValidateMediaDescriptor, self).__init__()

        self._xml_path = xml_path
        self.media_xml = ET.parse(xml_path).getroot()

        # Sanity checks
        self.media_xml.attrib["duration"]
        self.media_xml.attrib["seekable"]

    @staticmethod
    def new_from_uri(uri, verbose=False, full=False):
        media_path = utils.url2path(uri)
        descriptor_path = "%s.%s" % (
            media_path, GstValidateMediaDescriptor.MEDIA_INFO_EXT)
        args = GstValidateMediaDescriptor.DISCOVERER_COMMAND.split(" ")
        args.append(uri)

        args.extend(["--output-file", descriptor_path])
        if full:
            args.extend(["--full"])

        if verbose:
            printc("Generating media info for %s\n"
                   "    Command: '%s'" % (media_path, ' '.join(args)),
                   Colors.OKBLUE)

        try:
            subprocess.check_output(args, stderr=open(os.devnull))
        except subprocess.CalledProcessError as e:
            if verbose:
                printc("Result: Failed", Colors.FAIL)
            else:
                loggable.warning("GstValidateMediaDescriptor", "Exception: %s" % e)
            return None

        if verbose:
            printc("Result: Passed", Colors.OKGREEN)

        return GstValidateMediaDescriptor(descriptor_path)

    def get_path(self):
        return self._xml_path

    def need_clock_sync(self):
        return Protocols.needs_clock_sync(self.get_protocol())

    def get_media_filepath(self):
        if self.get_protocol() == Protocols.FILE:
            return self._xml_path.replace("." + self.MEDIA_INFO_EXT, "")
        else:
            return self._xml_path.replace("." + self.STREAM_INFO_EXT, "")

    def get_caps(self):
        return self.media_xml.findall("streams")[0].attrib["caps"]

    def get_tracks_caps(self):
        res = []
        try:
            streams = self.media_xml.findall("streams")[0].findall("stream")
        except IndexError:
            return res

        for stream in streams:
            res.append((stream.attrib["type"], stream.attrib["caps"]))

        return res

    def get_uri(self):
        return self.media_xml.attrib["uri"]

    def get_duration(self):
        return long(self.media_xml.attrib["duration"])

    def set_protocol(self, protocol):
        self.media_xml.attrib["protocol"] = protocol

    def get_protocol(self):
        return self.media_xml.attrib["protocol"]

    def is_seekable(self):
        return self.media_xml.attrib["seekable"]

    def is_image(self):
        for stream in self.media_xml.findall("streams")[0].findall("stream"):
            if stream.attrib["type"] == "image":
                return True
        return False

    def get_num_tracks(self, track_type):
        n = 0
        for stream in self.media_xml.findall("streams")[0].findall("stream"):
            if stream.attrib["type"] == track_type:
                n += 1

        return n

    def get_clean_name(self):
        name = os.path.basename(self.get_path())
        name = re.sub("\.stream_info|\.media_info", "", name)

        return name.replace('.', "_")


class MediaFormatCombination(object):
    FORMATS = {"aac": "audio/mpeg,mpegversion=4",
               "ac3": "audio/x-ac3",
               "vorbis": "audio/x-vorbis",
               "mp3": "audio/mpeg,mpegversion=1,layer=3",
               "h264": "video/x-h264",
               "vp8": "video/x-vp8",
               "theora": "video/x-theora",
               "ogg": "application/ogg",
               "mkv": "video/x-matroska",
               "mp4": "video/quicktime,variant=iso;",
               "webm": "video/webm"}

    def __str__(self):
        return "%s and %s in %s" % (self.audio, self.video, self.container)

    def __init__(self, container, audio, video):
        """
        Describes a media format to be used for transcoding tests.

        :param container: A string defining the container format to be used, must bin in self.FORMATS
        :param audio: A string defining the audio format to be used, must bin in self.FORMATS
        :param video: A string defining the video format to be used, must bin in self.FORMATS
        """
        self.container = container
        self.audio = audio
        self.video = video

    def get_caps(self, track_type):
        try:
            return self.FORMATS[self.__dict__[track_type]]
        except KeyError:
            return None

    def get_audio_caps(self):
        return self.get_caps("audio")

    def get_video_caps(self):
        return self.get_caps("video")

    def get_muxer_caps(self):
        return self.get_caps("container")
