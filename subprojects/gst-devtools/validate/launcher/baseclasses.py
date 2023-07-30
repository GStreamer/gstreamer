#!/usr/bin/env python3
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

from enum import Enum
import importlib.util
import json
import os
import sys
import re
import copy
import shlex
import socketserver
import struct
import time
from . import utils
import signal
import urllib.parse
import subprocess
import threading
import queue
import configparser
import xml
import random
import shutil
import uuid
from itertools import cycle
from fractions import Fraction

from .utils import GstCaps, which
from . import reporters
from . import loggable
from .loggable import Loggable

from collections import defaultdict
try:
    from lxml import etree as ET
except ImportError:
    import xml.etree.cElementTree as ET


from .vfb_server import get_virual_frame_buffer_server
from .httpserver import HTTPServer
from .utils import mkdir, Result, Colors, printc, DEFAULT_TIMEOUT, GST_SECOND, \
    Protocols, look_for_file_in_source_dir, get_data_file, BackTraceGenerator, \
    check_bugs_resolution, is_tty

# The factor by which we increase the hard timeout when running inside
# Valgrind
GDB_TIMEOUT_FACTOR = VALGRIND_TIMEOUT_FACTOR = 20
RR_TIMEOUT_FACTOR = 2
TIMEOUT_FACTOR = float(os.environ.get("TIMEOUT_FACTOR", 1))
# The error reported by valgrind when detecting errors
VALGRIND_ERROR_CODE = 20

VALIDATE_OVERRIDE_EXTENSION = ".override"
EXITING_SIGNALS = dict([(-getattr(signal, s), s) for s in [
    'SIGQUIT', 'SIGILL', 'SIGABRT', 'SIGFPE', 'SIGSEGV', 'SIGBUS', 'SIGSYS',
    'SIGTRAP', 'SIGXCPU', 'SIGXFSZ', 'SIGIOT'] if hasattr(signal, s)])
EXITING_SIGNALS.update({139: "SIGSEGV"})
EXITING_SIGNALS.update({(v, k) for k, v in EXITING_SIGNALS.items()})


CI_ARTIFACTS_URL = os.environ.get('CI_ARTIFACTS_URL')


class Test(Loggable):

    """ A class representing a particular test. """

    def __init__(self, application_name, classname, options,
                 reporter, duration=0, timeout=DEFAULT_TIMEOUT,
                 hard_timeout=None, extra_env_variables=None,
                 expected_issues=None, is_parallel=True,
                 workdir=None):
        """
        @timeout: The timeout during which the value return by get_current_value
                  keeps being exactly equal
        @hard_timeout: Max time the test can take in absolute
        """
        Loggable.__init__(self)
        self.timeout = timeout * TIMEOUT_FACTOR * options.timeout_factor
        if hard_timeout:
            self.hard_timeout = hard_timeout * TIMEOUT_FACTOR
            self.hard_timeout *= options.timeout_factor
        else:
            self.hard_timeout = hard_timeout
        self.classname = classname
        self.options = options
        self.application = application_name
        self.command = []
        self.server_command = None
        self.reporter = reporter
        self.process = None
        self.proc_env = None
        self.thread = None
        self.queue = None
        self.duration = duration
        self.stack_trace = None
        self._uuid = None
        if expected_issues is None:
            self.expected_issues = []
        elif not isinstance(expected_issues, list):
            self.expected_issues = [expected_issues]
        else:
            self.expected_issues = expected_issues

        extra_env_variables = extra_env_variables or {}
        self.extra_env_variables = extra_env_variables
        self.optional = False
        self.is_parallel = is_parallel
        self.generator = None
        self.workdir = workdir
        self.max_retries = 0
        self.html_log = None
        self.rr_logdir = None

        self.clean()

    def _generate_expected_issues(self):
        return ''

    def generate_expected_issues(self):
        res = '%s"FIXME \'%s\' issues [REPORT A BUG ' % (" " * 4, self.classname) \
            + 'in https://gitlab.freedesktop.org/gstreamer/ '\
            + 'or use a proper bug description]": {'
        res += """
        "tests": [
            "%s"
        ],
        "issues": [""" % (self.classname)

        retcode = self.process.returncode if self.process else 0
        if retcode != 0:
            signame = EXITING_SIGNALS.get(retcode)
            val = "'" + signame + "'" if signame else retcode
            res += """\n            {
                '%s': %s,
                'sometimes': True,
            },""" % ("signame" if signame else "returncode", val)

        res += self._generate_expected_issues()
        res += "\n%s],\n%s},\n" % (" " * 8, " " * 4)

        return res

    def copy(self, nth=None):
        copied_test = copy.copy(self)
        if nth:
            copied_test.classname += '_it' + str(nth)
            copied_test._uuid = None
            copied_test.options = copy.copy(self.options)
            copied_test.options.logsdir = os.path.join(copied_test.options.logsdir, str(nth))
            os.makedirs(copied_test.options.logsdir, exist_ok=True)

        return copied_test

    def clean(self):
        self.kill_subprocess()
        self.message = ""
        self.error_str = ""
        self.time_taken = 0.0
        self._starting_time = None
        self.result = Result.NOT_RUN
        self.logfile = None
        self.out = None
        self.extra_logfiles = set()
        self.__env_variable = []
        self.kill_subprocess()
        self.process = None

    def __str__(self):
        string = self.classname
        if self.result != Result.NOT_RUN:
            string += ": " + self.result
            if self.result in [Result.FAILED, Result.TIMEOUT]:
                string += " '%s'" % self.message
                if not self.options.dump_on_failure:
                    if not self.options.redirect_logs and self.result != Result.PASSED:
                        string += self.get_logfile_repr()
                else:
                    string = "\n==> %s" % string

        return string

    def add_env_variable(self, variable, value=None):
        """
        Only useful so that the gst-validate-launcher can print the exact
        right command line to reproduce the tests
        """
        if value is None:
            value = os.environ.get(variable, None)

        if value is None:
            return

        self.__env_variable.append(variable)

    @property
    def _env_variable(self):
        res = ""
        if not self.options.verbose or self.options.verbose > 1:
            for var in set(self.__env_variable):
                if res:
                    res += " "
                value = self.proc_env.get(var, None)
                if value is not None:
                    res += "%s='%s'" % (var, value)
        else:
            res += "[Not displaying environment variables, rerun with -vv for the full command]"

        return res

    def open_logfile(self):
        if self.out:
            return

        path = os.path.join(self.options.logsdir,
                            self.classname.replace(".", os.sep) + '.md')
        mkdir(os.path.dirname(path))
        self.logfile = path

        if self.options.redirect_logs == 'stdout':
            self.out = sys.stdout
        elif self.options.redirect_logs == 'stderr':
            self.out = sys.stderr
        else:
            self.out = open(path, 'w+')

    def finalize_logfiles(self):
        self.out.write("\n**Duration**: %s" % self.time_taken)
        if not self.options.redirect_logs:
            self.out.flush()
            for logfile in self.extra_logfiles:
                # Only copy over extra logfile content if it's below a certain threshold
                # Avoid copying gigabytes of data if a lot of debugging is activated
                if os.path.getsize(logfile) < 500 * 1024:
                    self.out.write('\n\n## %s:\n\n```\n%s\n```\n' % (
                        os.path.basename(logfile), self.get_extra_log_content(logfile))
                    )
                else:
                    self.out.write('\n\n## %s:\n\n**Log file too big.**\n  %s\n\n Check file content directly\n\n' % (
                        os.path.basename(logfile), logfile)
                    )

            if self.rr_logdir:
                self.out.write('\n\n## rr trace:\n\n```\nrr replay %s/latest-trace\n```\n' % (
                    self.rr_logdir))

            self.out.flush()
            self.out.close()

        if self.options.html:
            self.html_log = os.path.splitext(self.logfile)[0] + '.html'
            import commonmark
            parser = commonmark.Parser()
            with open(self.logfile) as f:
                ast = parser.parse(f.read())

            renderer = commonmark.HtmlRenderer()
            html = renderer.render(ast)
            with open(self.html_log, 'w') as f:
                f.write(html)

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

    def get_uuid(self):
        if self._uuid is None:
            self._uuid = self.classname + str(uuid.uuid4())
        return self._uuid

    def add_arguments(self, *args):
        self.command += args

    def build_arguments(self):
        self.add_env_variable("LD_PRELOAD")
        self.add_env_variable("DISPLAY")

    def add_stack_trace_to_logfile(self):
        self.debug("Adding stack trace")
        if self.options.rr:
            return

        trace_gatherer = BackTraceGenerator.get_default()
        stack_trace = trace_gatherer.get_trace(self)

        if not stack_trace:
            return

        info = "\n\n## Stack trace\n\n```\n%s\n```" % stack_trace
        if self.options.redirect_logs:
            print(info)
            return

        if self.options.xunit_file:
            self.stack_trace = stack_trace

        self.out.write(info)
        self.out.flush()

    def add_known_issue_information(self):
        if self.expected_issues:
            info = "\n\n## Already known issues\n\n``` python\n%s\n```\n\n" % (
                json.dumps(self.expected_issues, indent=4)
            )
        else:
            info = ""

        info += "\n\n**You can mark the issues as 'known' by adding the " \
            + f" following lines to the list of known issues of the testsuite called \"{self.classname.split('.')[0]}\"**\n" \
            + "\n\n``` python\n%s\n```" % (self.generate_expected_issues())

        if self.options.redirect_logs:
            print(info)
            return

        self.out.write(info)

    def set_result(self, result, message="", error=""):

        if not self.options.redirect_logs:
            self.out.write("\n```\n")
            self.out.flush()

        self.debug("Setting result: %s (message: %s, error: %s)" % (result,
                                                                    message, error))

        if result is Result.TIMEOUT:
            if self.options.debug is True:
                if self.options.gdb:
                    printc("Timeout, you should process <ctrl>c to get into gdb",
                           Colors.FAIL)
                    # and wait here until gdb exits
                    self.process.communicate()
                else:
                    pname = self.command[0]
                    input("%sTimeout happened  on %s you can attach gdb doing:\n $gdb %s %d%s\n"
                          "Press enter to continue" % (Colors.FAIL, self.classname,
                          pname, self.process.pid, Colors.ENDC))
            else:
                self.add_stack_trace_to_logfile()

        self.result = result
        self.message = message
        self.error_str = error

        if result not in [Result.PASSED, Result.NOT_RUN, Result.SKIPPED]:
            self.add_known_issue_information()

    def expected_return_codes(self):
        res = []
        for issue in self.expected_issues:
            if 'returncode' in issue:
                res.append(issue['returncode'])
        return res

    def check_results(self):
        if self.result is Result.FAILED or self.result is Result.TIMEOUT:
            return

        self.debug("%s returncode: %s", self, self.process.returncode)
        if self.options.rr and self.process.returncode == -signal.SIGPIPE:
            self.set_result(Result.SKIPPED, "SIGPIPE received under `rr`, known issue.")
        elif self.process.returncode == 0:
            for issue in self.expected_issues:
                if issue['returncode'] != 0 and not issue.get("sometimes", False):
                    self.set_result(Result.ERROR, "Expected return code %d" % issue['returncode'])
                    return
            self.set_result(Result.PASSED)
        elif self.process.returncode in EXITING_SIGNALS:
            self.add_stack_trace_to_logfile()
            self.set_result(Result.FAILED,
                            "Application exited with signal %s" % (
                                EXITING_SIGNALS[self.process.returncode]))
        elif self.process.returncode == VALGRIND_ERROR_CODE:
            self.set_result(Result.FAILED, "Valgrind reported errors")
        elif self.process.returncode in self.expected_return_codes():
            self.set_result(Result.KNOWN_ERROR)
        else:
            self.set_result(Result.FAILED,
                            "Application returned %d" % (self.process.returncode))

    def get_current_value(self):
        """
        Lets subclasses implement a nicer timeout measurement method
        They should return some value with which we will compare
        the previous and timeout if they are egual during self.timeout
        seconds
        """
        return Result.NOT_RUN

    def process_update(self):
        """
        Returns True when process has finished running or has timed out.
        """

        if self.process is None:
            # Process has not started running yet
            return False

        self.process.poll()
        if self.process.returncode is not None:
            return True

        val = self.get_current_value()

        self.debug("Got value: %s" % val)
        if val is Result.NOT_RUN:
            # The get_current_value logic is not implemented... dumb
            # timeout
            if time.time() - self.last_change_ts > self.timeout:
                self.set_result(Result.TIMEOUT,
                                "Application timed out: %s secs" %
                                self.timeout,
                                "timeout")
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
                self.set_result(Result.TIMEOUT,
                                "Application timed out: %s secs" %
                                self.timeout,
                                "timeout")
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
        return os.environ.copy()

    def kill_subprocess(self):
        subprocs_id = None
        if self.options.rr and self.process and self.process.returncode is None:
            cmd = ["ps", "-o", "pid", "--ppid", str(self.process.pid), "--noheaders"]
            try:
                subprocs_id = [int(pid.strip('\n')) for
                    pid in subprocess.check_output(cmd).decode().split(' ') if pid]
            except FileNotFoundError:
                self.error("Ps not found, will probably not be able to get rr "
                    "working properly after we kill the process")
            except subprocess.CalledProcessError as e:
                self.error("Couldn't get rr subprocess pid: %s" % (e))

        utils.kill_subprocess(self, self.process, DEFAULT_TIMEOUT, subprocs_id)

    def run_external_checks(self):
        pass

    def thread_wrapper(self):
        def enable_sigint():
            # Restore the SIGINT handler for the child process (gdb) to ensure
            # it can handle it.
            signal.signal(signal.SIGINT, signal.SIG_DFL)

        if self.options.gdb and os.name != "nt":
            preexec_fn = enable_sigint
        else:
            preexec_fn = None

        self.process = subprocess.Popen(self.command,
                                        stderr=self.out,
                                        stdout=self.out,
                                        env=self.proc_env,
                                        cwd=self.workdir,
                                        preexec_fn=preexec_fn)
        self.process.wait()
        if self.result is not Result.TIMEOUT:
            if self.process.returncode == 0:
                self.run_external_checks()
            self.queue.put(None)

    def get_valgrind_suppression_file(self, subdir, name):
        p = get_data_file(subdir, name)
        if p:
            return p

        self.error("Could not find any %s file" % name)

    def get_valgrind_suppressions(self):
        return [self.get_valgrind_suppression_file('data', 'gstvalidate.supp')]

    def use_gdb(self, command):
        if self.hard_timeout is not None:
            self.hard_timeout *= GDB_TIMEOUT_FACTOR
        self.timeout *= GDB_TIMEOUT_FACTOR

        if not self.options.gdb_non_stop:
            self.timeout = sys.maxsize
            self.hard_timeout = sys.maxsize

        args = ["gdb"]
        if self.options.gdb_non_stop:
            args += ["-ex", "run", "-ex", "backtrace", "-ex", "quit"]
        args += ["--args"] + command
        return args

    def use_rr(self, command, subenv):
        command = ["rr", 'record', '-h'] + command

        self.timeout *= RR_TIMEOUT_FACTOR
        self.rr_logdir = os.path.join(self.options.logsdir, self.classname.replace(".", os.sep), 'rr-logs')
        subenv['_RR_TRACE_DIR'] = self.rr_logdir
        try:
            shutil.rmtree(self.rr_logdir, ignore_errors=False, onerror=None)
        except FileNotFoundError:
            pass
        self.add_env_variable('_RR_TRACE_DIR', self.rr_logdir)

        return command

    def use_valgrind(self, command, subenv):
        vglogsfile = os.path.splitext(self.logfile)[0] + '.valgrind'
        self.extra_logfiles.add(vglogsfile)

        vg_args = []

        for o, v in [('trace-children', 'yes'),
                     ('tool', 'memcheck'),
                     ('leak-check', 'full'),
                     ('leak-resolution', 'high'),
                     # TODO: errors-for-leak-kinds should be set to all instead of definite
                     # and all false positives should be added to suppression
                     # files.
                     ('errors-for-leak-kinds', 'definite,indirect'),
                     ('show-leak-kinds', 'definite,indirect'),
                     ('show-possibly-lost', 'no'),
                     ('num-callers', '20'),
                     ('error-exitcode', str(VALGRIND_ERROR_CODE)),
                     ('gen-suppressions', 'all')]:
            vg_args.append("--%s=%s" % (o, v))

        if not self.options.redirect_logs:
            vglogsfile = os.path.splitext(self.logfile)[0] + '.valgrind'
            self.extra_logfiles.add(vglogsfile)
            vg_args.append("--%s=%s" % ('log-file', vglogsfile))

        for supp in self.get_valgrind_suppressions():
            vg_args.append("--suppressions=%s" % supp)

        command = ["valgrind"] + vg_args + command

        # Tune GLib's memory allocator to be more valgrind friendly
        subenv['G_DEBUG'] = 'gc-friendly'
        subenv['G_SLICE'] = 'always-malloc'

        if self.hard_timeout is not None:
            self.hard_timeout *= VALGRIND_TIMEOUT_FACTOR
        self.timeout *= VALGRIND_TIMEOUT_FACTOR

        # Enable 'valgrind.config'
        self.add_validate_config(get_data_file(
            'data', 'valgrind.config'), subenv)
        if subenv == self.proc_env:
            self.add_env_variable('G_DEBUG', 'gc-friendly')
            self.add_env_variable('G_SLICE', 'always-malloc')
            self.add_env_variable('GST_VALIDATE_CONFIG',
                                  self.proc_env['GST_VALIDATE_CONFIG'])

        return command

    def add_validate_config(self, config, subenv=None):
        if not subenv:
            subenv = self.extra_env_variables

        cconf = subenv.get('GST_VALIDATE_CONFIG', "")
        paths = [c for c in cconf.split(os.pathsep) if c] + [config]
        subenv['GST_VALIDATE_CONFIG'] = os.pathsep.join(paths)

    def launch_server(self):
        return None

    def get_logfile_repr(self):
        if not self.options.redirect_logs:
            if self.html_log:
                log = self.html_log
            else:
                log = self.logfile

            if CI_ARTIFACTS_URL:
                log = CI_ARTIFACTS_URL + os.path.relpath(log, self.options.logsdir)

            return "\n    Log: %s" % (log)

        return ""

    def get_command_repr(self):
        message = "%s %s" % (self._env_variable, ' '.join(
            shlex.quote(arg) for arg in self.command))
        if self.server_command:
            message = "%s & %s" % (self.server_command, message)

        return message

    def test_start(self, queue):
        self.open_logfile()

        self.server_command = self.launch_server()
        self.queue = queue
        self.command = [self.application]
        self._starting_time = time.time()
        self.build_arguments()
        self.proc_env = self.get_subproc_env()

        for var, value in list(self.extra_env_variables.items()):
            value = self.proc_env.get(var, '') + os.pathsep + value
            self.proc_env[var] = value.strip(os.pathsep)
            self.add_env_variable(var, self.proc_env[var])

        if self.options.gdb:
            self.command = self.use_gdb(self.command)

            self.previous_sigint_handler = signal.getsignal(signal.SIGINT)
            # Make the gst-validate executable ignore SIGINT while gdb is
            # running.
            signal.signal(signal.SIGINT, signal.SIG_IGN)

        if self.options.valgrind:
            self.command = self.use_valgrind(self.command, self.proc_env)

        if self.options.rr:
            self.command = self.use_rr(self.command, self.proc_env)

        if not self.options.redirect_logs:
            self.out.write("# `%s`\n\n"
                           "## Command\n\n``` bash\n%s\n```\n\n" % (
                               self.classname, self.get_command_repr()))
            self.out.write("## %s output\n\n``` log \n\n" % os.path.basename(self.application))
            self.out.flush()
        else:
            message = "Launching: %s%s\n" \
                "    Command: %s\n" % (Colors.ENDC, self.classname,
                                       self.get_command_repr())
            printc(message, Colors.OKBLUE)

        self.thread = threading.Thread(target=self.thread_wrapper)
        self.thread.start()

        self.last_val = 0
        self.last_change_ts = time.time()
        self.start_ts = time.time()

    def _dump_log_file(self, logfile):
        if which('bat'):
            try:
                subprocess.check_call(['bat', '-H', '1', '--paging=never', logfile])
                return
            except (subprocess.CalledProcessError, FileNotFoundError):
                pass

        with open(logfile, 'r') as fin:
            for line in fin.readlines():
                print('> ' + line, end='')

    def _dump_log_files(self):
        self._dump_log_file(self.logfile)

    def copy_logfiles(self, extra_folder="flaky_tests"):
        path = os.path.dirname(os.path.join(self.options.logsdir, extra_folder,
                            self.classname.replace(".", os.sep)))
        mkdir(path)
        self.logfile = shutil.copy(self.logfile, path)
        extra_logs = []
        for logfile in self.extra_logfiles:
            extra_logs.append(shutil.copy(logfile, path))
        self.extra_logfiles = extra_logs

    def test_end(self, retry_on_failures=False):
        self.kill_subprocess()
        self.thread.join()
        self.time_taken = time.time() - self._starting_time

        if self.options.gdb:
            signal.signal(signal.SIGINT, self.previous_sigint_handler)

        self.finalize_logfiles()
        if self.options.dump_on_failure and not retry_on_failures and not self.max_retries:
            if self.result not in [Result.PASSED, Result.KNOWN_ERROR, Result.NOT_RUN]:
                self._dump_log_files()

        # Only keep around env variables we need later
        clean_env = {}
        for n in self.__env_variable:
            clean_env[n] = self.proc_env.get(n, None)
        self.proc_env = clean_env

        return self.result


class GstValidateTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    pass


class GstValidateListener(socketserver.BaseRequestHandler, Loggable):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        Loggable.__init__(self, "GstValidateListener")

    def handle(self):
        """Implements BaseRequestHandler handle method"""
        test = None
        self.logCategory = "GstValidateListener"
        while True:
            raw_len = self.request.recv(4)
            if raw_len == b'':
                return
            msglen = struct.unpack('>I', raw_len)[0]
            e = None
            raw_msg = bytes()
            while msglen != len(raw_msg):
                raw_msg += self.request.recv(msglen - len(raw_msg))
            if e is not None:
                continue
            try:
                msg = raw_msg.decode('utf-8', 'ignore')
            except UnicodeDecodeError as e:
                self.error("%s Could not decode message: %s - %s" % (test.classname if test else "unknown", msg, e))
                continue

            if msg == '':
                return

            try:
                obj = json.loads(msg)
            except json.decoder.JSONDecodeError as e:
                self.error("%s Could not decode message: %s - %s" % (test.classname if test else "unknown", msg, e))
                continue

            if test is None:
                # First message must contain the uuid
                uuid = obj.get("uuid", None)
                if uuid is None:
                    return
                # Find test from launcher
                for t in self.server.launcher.tests:
                    if uuid == t.get_uuid():
                        test = t
                        break
                if test is None:
                    self.server.launcher.error(
                        "Could not find test for UUID %s" % uuid)
                    return

            obj_type = obj.get("type", '')
            if obj_type == 'position':
                test.set_position(obj['position'], obj['duration'],
                                  obj['speed'])
            elif obj_type == 'buffering':
                test.set_position(obj['position'], 100)
            elif obj_type == 'action':
                test.add_action_execution(obj)
                # Make sure that action is taken into account when checking if process
                # is updating
                test.position += 1
            elif obj_type == 'action-done':
                # Make sure that action end is taken into account when checking if process
                # is updating
                test.position += 1
                if test.actions_infos:
                    test.actions_infos[-1]['execution-duration'] = obj['execution-duration']
            elif obj_type == 'report':
                test.add_report(obj)
            elif obj_type == 'skip-test':
                test.set_result(Result.SKIPPED)


class GstValidateTest(Test):

    """ A class representing a particular test. """
    HARD_TIMEOUT_FACTOR = 5
    fault_sig_regex = re.compile("<Caught SIGNAL: .*>")
    needs_gst_inspect = set()

    def __init__(self, application_name, classname,
                 options, reporter, duration=0,
                 timeout=DEFAULT_TIMEOUT, scenario=None, hard_timeout=None,
                 media_descriptor=None, extra_env_variables=None,
                 expected_issues=None, workdir=None, **kwargs):

        extra_env_variables = extra_env_variables or {}

        if not hard_timeout and self.HARD_TIMEOUT_FACTOR:
            if timeout:
                hard_timeout = timeout * self.HARD_TIMEOUT_FACTOR
            elif duration:
                hard_timeout = duration * self.HARD_TIMEOUT_FACTOR
            else:
                hard_timeout = None

        # If we are running from source, use the -debug version of the
        # application which is using rpath instead of libtool's wrappers. It's
        # slightly faster to start and will not confuse valgrind.
        debug = '%s-debug' % application_name
        p = look_for_file_in_source_dir('tools', debug)
        if p:
            application_name = p

        self.reports = []
        self.position = -1
        self.media_duration = -1
        self.speed = 1.0
        self.actions_infos = []
        self.media_descriptor = media_descriptor
        self.server = None
        self.criticals = []

        override_path = self.get_override_file(media_descriptor)
        if override_path:
            if extra_env_variables:
                if extra_env_variables.get("GST_VALIDATE_OVERRIDE", ""):
                    extra_env_variables[
                        "GST_VALIDATE_OVERRIDE"] += os.path.pathsep

            extra_env_variables["GST_VALIDATE_OVERRIDE"] = override_path

        super().__init__(application_name,
                         classname,
                         options, reporter,
                         duration=duration,
                         timeout=timeout,
                         hard_timeout=hard_timeout,
                         extra_env_variables=extra_env_variables,
                         expected_issues=expected_issues,
                         workdir=workdir,
                         **kwargs)
        if media_descriptor and media_descriptor.get_media_filepath():
            config_file = os.path.join(media_descriptor.get_media_filepath() + '.config')
            if os.path.isfile(config_file):
                self.add_validate_config(config_file, extra_env_variables)

        if scenario is None or scenario.name.lower() == "none":
            self.scenario = None
        else:
            self.scenario = scenario

    def needs_http_server(self):
        if self.media_descriptor is None:
            return False

        protocol = self.media_descriptor.get_protocol()
        uri = self.media_descriptor.get_uri()
        uri_requires_http_server = False
        if uri:
            if 'http-server-port' in uri:
                expanded_uri = uri % {
                    'http-server-port': self.options.http_server_port}
                uri_requires_http_server = expanded_uri.find(
                    "127.0.0.1:%s" % self.options.http_server_port) != -1
        if protocol in [Protocols.HTTP, Protocols.HLS, Protocols.DASH] or uri_requires_http_server:
            return True

        return False

    def kill_subprocess(self):
        Test.kill_subprocess(self)

    def add_report(self, report):
        self.reports.append(report)

    def set_position(self, position, duration, speed=None):
        self.position = position
        self.media_duration = duration
        if speed:
            self.speed = speed

    def add_action_execution(self, action_infos):
        self.actions_infos.append(action_infos)

    def get_override_file(self, media_descriptor):
        if media_descriptor:
            if media_descriptor.get_path():
                override_path = os.path.splitext(media_descriptor.get_path())[
                    0] + VALIDATE_OVERRIDE_EXTENSION
                if os.path.exists(override_path):
                    return override_path

        return None

    def get_current_position(self):
        return self.position

    def get_current_value(self):
        return self.position

    def get_subproc_env(self):
        subproc_env = os.environ.copy()

        if self.options.validate_default_config:
            self.add_validate_config(self.options.validate_default_config,
                subproc_env, )

        subproc_env["GST_VALIDATE_UUID"] = self.get_uuid()
        subproc_env["GST_VALIDATE_LOGSDIR"] = self.options.logsdir

        no_color = True
        if 'GST_DEBUG' in os.environ and not self.options.redirect_logs and not self.options.debug:
            gstlogsfile = os.path.splitext(self.logfile)[0] + '.gstdebug'
            self.extra_logfiles.add(gstlogsfile)
            subproc_env["GST_DEBUG_FILE"] = gstlogsfile
            no_color = self.options.no_color

        if no_color:
            subproc_env["GST_DEBUG_NO_COLOR"] = '1'

        # Ensure XInitThreads is called, see bgo#731525
        subproc_env['GST_GL_XINITTHREADS'] = '1'
        self.add_env_variable('GST_GL_XINITTHREADS', '1')
        subproc_env['GST_XINITTHREADS'] = '1'
        self.add_env_variable('GST_XINITTHREADS', '1')

        if self.scenario is not None:
            scenario = self.scenario.get_execution_name()
            subproc_env["GST_VALIDATE_SCENARIO"] = scenario
            self.add_env_variable("GST_VALIDATE_SCENARIO",
                                  subproc_env["GST_VALIDATE_SCENARIO"])
        else:
            try:
                del subproc_env["GST_VALIDATE_SCENARIO"]
            except KeyError:
                pass

        if not subproc_env.get('GST_DEBUG_DUMP_DOT_DIR'):
            dotfilesdir = os.path.join(self.options.logsdir,
                                self.classname.replace(".", os.sep) + '.pipelines_dot_files')
            mkdir(dotfilesdir)
            subproc_env['GST_DEBUG_DUMP_DOT_DIR'] = dotfilesdir
            if CI_ARTIFACTS_URL:
                dotfilesurl = CI_ARTIFACTS_URL + os.path.relpath(dotfilesdir,
                                                                 self.options.logsdir)
                subproc_env['GST_VALIDATE_DEBUG_DUMP_DOT_URL'] = dotfilesurl

        return subproc_env

    def clean(self):
        Test.clean(self)
        self.reports = []
        self.position = -1
        self.media_duration = -1
        self.speed = 1.0
        self.actions_infos = []

    def copy(self, nth=None):
        new_test = super().copy(nth=nth)
        new_test.reports = copy.deepcopy(self.reports)
        return new_test

    def build_arguments(self):
        super(GstValidateTest, self).build_arguments()
        if "GST_VALIDATE" in os.environ:
            self.add_env_variable("GST_VALIDATE", os.environ["GST_VALIDATE"])

        if "GST_VALIDATE_SCENARIOS_PATH" in os.environ:
            self.add_env_variable("GST_VALIDATE_SCENARIOS_PATH",
                                  os.environ["GST_VALIDATE_SCENARIOS_PATH"])

        self.add_env_variable("GST_VALIDATE_CONFIG")
        self.add_env_variable("GST_VALIDATE_OVERRIDE")

    def get_extra_log_content(self, extralog):
        value = Test.get_extra_log_content(self, extralog)

        return value

    def report_matches_expected_issues(self, report, expected_issue):
        for key in ['bug', 'bugs', 'sometimes']:
            if key in expected_issue:
                del expected_issue[key]
        for key, value in list(report.items()):
            if key in expected_issue:
                if not re.findall(expected_issue[key], str(value)):
                    return False
                expected_issue.pop(key)

        if "can-happen-several-times" in expected_issue:
            expected_issue.pop("can-happen-several-times")
        return not bool(expected_issue)

    def check_reported_issues(self, expected_issues):
        ret = []
        expected_retcode = [0]
        for report in self.reports:
            found = None
            for expected_issue in expected_issues:
                if self.report_matches_expected_issues(report,
                                                       expected_issue.copy()):
                    found = expected_issue
                    break

            if found is not None:
                if not found.get('can-happen-several-times', False):
                    expected_issues.remove(found)
                if report['level'] == 'critical':
                    if found.get('sometimes', True) and isinstance(expected_retcode, list):
                        expected_retcode.append(18)
                    else:
                        expected_retcode = [18]
            elif report['level'] == 'critical':
                ret.append(report)

        if not ret:
            return None, expected_issues, expected_retcode

        return ret, expected_issues, expected_retcode

    def check_expected_issue(self, expected_issue):
        res = True
        msg = ''
        expected_symbols = expected_issue.get('stacktrace_symbols')
        if expected_symbols:
            trace_gatherer = BackTraceGenerator.get_default()
            stack_trace = trace_gatherer.get_trace(self)

            if stack_trace:
                if not isinstance(expected_symbols, list):
                    expected_symbols = [expected_symbols]

                not_found_symbols = [s for s in expected_symbols
                                     if s not in stack_trace]
                if not_found_symbols:
                    msg = " Expected symbols '%s' not found in stack trace " % (
                        not_found_symbols)
                    res = False
            else:
                msg += " No stack trace available, could not verify symbols "

        _, not_found_expected_issues, _ = self.check_reported_issues(expected_issue.get('issues', []))
        if not_found_expected_issues:
            mandatory_failures = [f for f in not_found_expected_issues
                                  if not f.get('sometimes', True)]
            if mandatory_failures:
                msg = " (Expected issues not found: %s) " % mandatory_failures
                res = False

        return msg, res

    def check_expected_timeout(self, expected_timeout):
        msg = "Expected timeout happened. "
        result = Result.PASSED
        message = expected_timeout.get('message')
        if message:
            if not re.findall(message, self.message):
                result = Result.FAILED
                msg = "Expected timeout message: %s got %s " % (
                    message, self.message)

        stack_msg, stack_res = self.check_expected_issue(expected_timeout)
        if not stack_res:
            result = Result.TIMEOUT
            msg += stack_msg

        return result, msg

    def check_results(self):
        if self.result in [Result.FAILED, Result.PASSED, Result.SKIPPED]:
            return

        self.debug("%s returncode: %s", self, self.process.returncode)
        expected_issues = copy.deepcopy(self.expected_issues)
        if self.options.rr:
            # signal.SIGPPIPE is 13 but it sometimes isn't present in python for some reason.
            expected_issues.append({"returncode": -13, "sometimes": True})
        self.criticals, not_found_expected_issues, expected_returncode = self.check_reported_issues(expected_issues)
        expected_timeout = None
        expected_signal = None
        for i, f in enumerate(not_found_expected_issues):
            returncode = f.get('returncode', [])
            if not isinstance(returncode, list):
                returncode = [returncode]

            if f.get('signame'):
                signames = f['signame']
                if not isinstance(signames, list):
                    signames = [signames]

                returncode = [EXITING_SIGNALS[signame] for signame in signames]

            if returncode:
                if 'sometimes' in f:
                    returncode.append(0)
                expected_returncode = returncode
                expected_signal = f
            elif f.get("timeout"):
                expected_timeout = f

        not_found_expected_issues = [f for f in not_found_expected_issues
                                     if not f.get('returncode') and not f.get('signame')]

        msg = ""
        result = Result.PASSED
        if self.result == Result.TIMEOUT:
            with open(self.logfile) as f:
                signal_fault_info = self.fault_sig_regex.findall(f.read())
                if signal_fault_info:
                    result = Result.FAILED
                    msg = signal_fault_info[0]
                elif expected_timeout:
                    not_found_expected_issues.remove(expected_timeout)
                    result, msg = self.check_expected_timeout(expected_timeout)
                else:
                    return
        elif self.process.returncode in EXITING_SIGNALS:
            msg = "Application exited with signal %s" % (
                EXITING_SIGNALS[self.process.returncode])
            if self.process.returncode not in expected_returncode:
                result = Result.FAILED
            else:
                if expected_signal:
                    stack_msg, stack_res = self.check_expected_issue(
                        expected_signal)
                    if not stack_res:
                        msg += stack_msg
                        result = Result.FAILED
            self.add_stack_trace_to_logfile()
        elif self.process.returncode == VALGRIND_ERROR_CODE:
            msg = "Valgrind reported errors "
            result = Result.FAILED
        elif self.process.returncode not in expected_returncode:
            msg = "Application returned %s " % self.process.returncode
            if expected_returncode != [0]:
                msg += "(expected %s) " % expected_returncode
            result = Result.FAILED

        if self.criticals:
            msg += "(critical errors: [%s]) " % ', '.join(set([c['summary']
                                                           for c in self.criticals]))
            result = Result.FAILED

        if not_found_expected_issues:
            mandatory_failures = [f for f in not_found_expected_issues
                                  if not f.get('sometimes', True)]

            if mandatory_failures:
                msg += " (Expected errors not found: %s) " % mandatory_failures
                result = Result.FAILED
        elif self.expected_issues:
            msg += ' %s(Expected errors occurred: %s)%s' % (Colors.OKBLUE,
                                                            self.expected_issues,
                                                            Colors.ENDC)
            result = Result.KNOWN_ERROR

        if result == Result.PASSED:
            for report in self.reports:
                if report["level"] == "expected":
                    result = Result.KNOWN_ERROR
                    break

        self.set_result(result, msg.strip())

    def _generate_expected_issues(self):
        res = ""
        self.criticals = self.criticals or []
        if self.result == Result.TIMEOUT:
            res += """            {
                'timeout': True,
                'sometimes': True,
            },"""

        for report in self.criticals:
            res += "\n%s{" % (" " * 12)

            for key, value in report.items():
                if key == "type":
                    continue
                if value is None:
                    continue
                res += '\n%s%s"%s": "%s",' % (
                    " " * 16, "# " if key == "details" else "",
                    key, value.replace('\n', '\\n'))

            res += "\n%s}," % (" " * 12)

        return res

    def get_valgrind_suppressions(self):
        result = super(GstValidateTest, self).get_valgrind_suppressions()
        result.extend(utils.get_gst_build_valgrind_suppressions())
        return result

    def test_end(self, retry_on_failures=False):
        ret = super().test_end(retry_on_failures=retry_on_failures)

        # Don't keep around JSON report objects, they were processed
        # in check_results already
        self.reports = []

        return ret


class VariableFramerateMode(Enum):
    DISABLED = 1
    ENABLED = 2
    AUTO = 3


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
            size = os.stat(urllib.parse.urlparse(self.dest_file).path).st_size
        except OSError:
            return None

        self.debug("Size: %s" % size)
        return size

    def _get_profile_full(self, muxer, venc, aenc, video_restriction=None,
                          audio_restriction=None, audio_presence=0,
                          video_presence=0,
                          variable_framerate=VariableFramerateMode.DISABLED):

        ret = ""
        if muxer:
            ret += muxer
        ret += ":"
        if venc:
            if video_restriction is not None:
                ret = ret + video_restriction + '->'
            ret += venc
            props = ""
            if video_presence:
                props += 'presence=%s|' % str(video_presence)
            if variable_framerate == VariableFramerateMode.AUTO:
                if video_restriction and "framerate" in video_restriction:
                    variable_framerate = VariableFramerateMode.DISABLED
                else:
                    variable_framerate = VariableFramerateMode.ENABLED
            if variable_framerate == VariableFramerateMode.ENABLED:
                props += 'variable-framerate=true|'
            if props:
                ret = ret + '|' + props[:-1]
        if aenc:
            ret += ":"
            if audio_restriction is not None:
                ret = ret + audio_restriction + '->'
            ret += aenc
            if audio_presence:
                ret = ret + '|' + str(audio_presence)

        return ret.replace("::", ":")

    def get_profile(self, video_restriction=None, audio_restriction=None,
            variable_framerate=VariableFramerateMode.DISABLED):
        vcaps = self.combination.get_video_caps()
        acaps = self.combination.get_audio_caps()
        if video_restriction is None:
            video_restriction = self.combination.video_restriction
        if audio_restriction is None:
            audio_restriction = self.combination.audio_restriction
        if self.media_descriptor is not None:
            if self.combination.video == "theora":
                # Theoraenc doesn't support variable framerate, make sure to avoid them
                framerate = self.media_descriptor.get_framerate()
                if framerate == Fraction(0, 1):
                    framerate = Fraction(30, 1)
                    restriction = utils.GstCaps.new_from_str(video_restriction or "video/x-raw")
                    for struct, _ in restriction:
                        if struct.get("framerate") is None:
                            struct.set("framerate", struct.FRACTION_TYPE, framerate)
                    video_restriction = str(restriction)

            video_presence = self.media_descriptor.get_num_tracks("video")
            if video_presence == 0:
                vcaps = None

            audio_presence = self.media_descriptor.get_num_tracks("audio")
            if audio_presence == 0:
                acaps = None

        return self._get_profile_full(self.combination.get_muxer_caps(),
                                      vcaps, acaps,
                                      audio_presence=audio_presence,
                                      video_presence=video_presence,
                                      video_restriction=video_restriction,
                                      audio_restriction=audio_restriction,
                                      variable_framerate=variable_framerate)

    def _clean_caps(self, caps):
        """
        Returns a list of key=value or structure name, without "(types)" or ";" or ","
        """
        return re.sub(r"\(.+?\)\s*| |;", '', caps).split(',')

    # pylint: disable=E1101
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
                        "Found %s in %s, good enough!", possible_c_variant, ccaps)
                    has_variant = True

        return has_variant

    # pylint: disable=E1101
    def run_iqa_test(self, reference_file_uri):
        """
        Runs IQA test if @reference_file_path exists
        @test: The test to run tests on
        """
        if not GstValidateBaseTestManager.has_feature('iqa'):
            self.debug('Iqa element not present, not running extra test.')
            return

        pipeline_desc = """
            uridecodebin uri=%s !
                iqa name=iqa do-dssim=true dssim-error-threshold=1.0 ! fakesink
            uridecodebin uri=%s ! iqa.
        """ % (reference_file_uri, self.dest_file)
        pipeline_desc = pipeline_desc.replace("\n", "")

        command = [GstValidateBaseTestManager.COMMAND] + \
            shlex.split(pipeline_desc)
        msg = "## Running IQA tests on results of: " \
            + "%s\n### Command: \n```\n%s\n```\n" % (
                self.classname, ' '.join(command))
        if not self.options.redirect_logs:
            self.out.write(msg)
            self.out.flush()
        else:
            printc(msg, Colors.OKBLUE)

        self.process = subprocess.Popen(command,
                                        stderr=self.out,
                                        stdout=self.out,
                                        env=self.proc_env,
                                        cwd=self.workdir)
        self.process.wait()

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
            self.add_report(
                {
                    'type': 'report',
                    'issue-id': 'transcoded-file-wrong-duration',
                    'summary': 'The duration of a transcoded file doesn\'t match the duration of the original file',
                    'level': 'critical',
                    'detected-on': 'pipeline',
                    'details': "Duration of encoded file is " " wrong (%s instead of %s)" % (
                        utils.TIME_ARGS(duration), utils.TIME_ARGS(orig_duration))
                }
            )
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
                    self.add_report(
                        {
                            'type': 'report',
                            'issue-id': 'transcoded-file-wrong-stream-type',
                            'summary': 'Expected stream types during transcoding do not match expectations',
                            'level': 'critical',
                            'detected-on': 'pipeline',
                            'details': "Found a track of type %s in the encoded files"
                                    " but none where wanted in the encoded profile: %s" % (
                                        track_type, self.combination)
                        }
                    )
                    return

                for c in cwanted_caps:
                    if c not in ccaps:
                        if not self._has_caps_type_variant(c, ccaps):
                            os.remove(result_descriptor.get_path())
                            self.add_report(
                                {
                                    'type': 'report',
                                    'issue-id': 'transcoded-file-wrong-caps',
                                    'summary': 'Expected stream caps during transcoding do not match expectations',
                                    'level': 'critical',
                                    'detected-on': 'pipeline',
                                    'details': "Field: %s  (from %s) not in caps of the outputted file %s" % (
                                        wanted_caps, c, ccaps)
                                }
                            )
                            return

            os.remove(result_descriptor.get_path())


class TestsManager(Loggable):

    """ A class responsible for managing tests. """

    name = "base"
    loading_testsuite = None

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
        self.check_testslist = True
        self.all_tests = None
        self.expected_issues = {}
        self.blacklisted_tests = []

    def init(self):
        return True

    def list_tests(self):
        return sorted(list(self.tests), key=lambda x: x.classname)

    def find_tests(self, classname):
        regex = re.compile(classname)
        return [test for test in self.list_tests() if regex.findall(test.classname)]

    def add_expected_issues(self, expected_issues):
        for bugid, failure_def in list(expected_issues.items()):
            tests_regexes = []
            for test_name_regex in failure_def['tests']:
                regex = re.compile(test_name_regex)
                tests_regexes.append(regex)
                for test in self.tests:
                    if regex.findall(test.classname):
                        max_retries = failure_def.get('allow_flakiness', failure_def.get('max_retries'))
                        if max_retries:
                            test.max_retries = int(max_retries)
                            self.debug(f"{test.classname} allow {test.max_retries}")
                        else:
                            for issue in failure_def['issues']:
                                issue['bug'] = bugid
                            test.expected_issues.extend(failure_def['issues'])
                            self.debug("%s added expected issues from %s" % (
                                test.classname, bugid))
            failure_def['tests'] = tests_regexes

        self.expected_issues.update(expected_issues)

    def add_test(self, test):
        if test.generator is None:
            test.classname = self.loading_testsuite + '.' + test.classname

        for bugid, failure_def in list(self.expected_issues.items()):
            failure_def['bug'] = bugid
            for regex in failure_def['tests']:
                if regex.findall(test.classname):
                    max_retries = failure_def.get('allow_flakiness', failure_def.get('max_retries'))
                    if max_retries:
                        test.max_retries = int(max_retries)
                        self.debug(f"{test.classname} allow {test.max_retries} retries.")
                    else:
                        for issue in failure_def['issues']:
                            issue['bug'] = bugid
                        test.expected_issues.extend(failure_def['issues'])
                        self.debug("%s added expected issues from %s" % (
                            test.classname, bugid))

        if self._is_test_wanted(test):
            if test not in self.tests:
                self.tests.append(test)
        else:
            if test not in self.tests:
                self.unwanted_tests.append(test)

    def get_tests(self):
        return self.tests

    def populate_testsuite(self):
        pass

    def add_generators(self, generators):
        """
        @generators: A list of, or one single #TestsGenerator to be used to generate tests
        """
        if not isinstance(generators, list):
            generators = [generators]
        self._generators.extend(generators)
        for generator in generators:
            generator.testsuite = self.loading_testsuite

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
        for test_regex, reason, *re_flags in default_blacklist:
            re_flags = re_flags[0] if re_flags else None

            if not test_regex.startswith(self.loading_testsuite + '.'):
                test_regex = self.loading_testsuite + '.' + test_regex
            if re_flags is not None:
                test_regex = re_flags + test_regex
            self.blacklisted_tests.append((test_regex, reason))
            self._add_blacklist(test_regex)

    def add_options(self, parser):
        """ Add more arguments. """
        pass

    def set_settings(self, options, args, reporter):
        """ Set properties after options parsing. """
        self.options = options
        self.args = args
        self.reporter = reporter

        self.populate_testsuite()

        if self.options.valgrind:
            self.print_valgrind_bugs()

        if options.wanted_tests:
            for patterns in options.wanted_tests:
                for pattern in patterns.split(","):
                    self.wanted_tests_patterns.append(re.compile(pattern))

        if options.blacklisted_tests:
            for patterns in options.blacklisted_tests:
                self._add_blacklist(patterns)

    def check_blacklists(self):
        if self.options.check_bugs_status:
            if not check_bugs_resolution(self.blacklisted_tests):
                return False

        return True

    def log_blacklists(self):
        if self.blacklisted_tests:
            self.info("Currently 'hardcoded' %s blacklisted tests:" %
                      self.name)

        for name, bug in self.blacklisted_tests:
            if not self.options.check_bugs_status:
                self.info("  + %s --> bug: %s" % (name, bug))

    def check_expected_issues(self):
        if not self.expected_issues or not self.options.check_bugs_status:
            return True

        bugs_definitions = defaultdict(list)
        for bug, failure_def in list(self.expected_issues.items()):
            tests_names = '|'.join(
                [regex.pattern for regex in failure_def['tests']])
            bugs_definitions[tests_names].extend([bug])

        return check_bugs_resolution(bugs_definitions.items())

    def _check_blacklisted(self, test):
        for pattern in self.blacklisted_tests_patterns:
            if pattern.findall(test.classname):
                self.info("%s is blacklisted by %s", test.classname, pattern)
                return True

        return False

    def _check_whitelisted(self, test):
        for pattern in self.wanted_tests_patterns:
            if pattern.findall(test.classname):
                if self._check_blacklisted(test):
                    # If explicitly white listed that specific test
                    # bypass the blacklisting
                    if pattern.pattern != test.classname:
                        return False
                return True
        return False

    def _check_duration(self, test):
        if test.duration > 0 and int(self.options.long_limit) < int(test.duration):
            self.info("Not activating %s as its duration (%d) is superior"
                      " than the long limit (%d)" % (test, test.duration,
                                                     int(self.options.long_limit)))
            return False

        return True

    def _is_test_wanted(self, test):
        if self._check_whitelisted(test):
            if not self._check_duration(test):
                return False
            return True

        if self._check_blacklisted(test):
            return False

        if not self._check_duration(test):
            return False

        if not self.wanted_tests_patterns:
            return True

        return False

    def needs_http_server(self):
        return False

    def print_valgrind_bugs(self):
        pass


class TestsGenerator(Loggable):

    def __init__(self, name, test_manager, tests=[]):
        Loggable.__init__(self)
        self.name = name
        self.test_manager = test_manager
        self.testsuite = None
        self._tests = {}
        for test in tests:
            self._tests[test.classname] = test

    def generate_tests(self, *kwargs):
        """
        Method that generates tests
        """
        return list(self._tests.values())

    def add_test(self, test):
        test.generator = self
        test.classname = self.testsuite + '.' + test.classname
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
        self.all_tests = None
        self.wanted_tests_patterns = []

        self.queue = queue.Queue()
        self.jobs = []
        self.total_num_tests = 0
        self.current_progress = -1
        self.server = None
        self.httpsrv = None
        self.vfb_server = None

    def _list_app_dirs(self):
        app_dirs = []
        env_dirs = os.environ["GST_VALIDATE_APPS_DIR"]
        if env_dirs is not None:
            for dir_ in env_dirs.split(os.pathsep):
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
                exec(compile(open(os.path.join(app_dir, f)).read(),
                             os.path.join(app_dir, f), 'exec'), env)

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

    def _load_testsuite(self, testsuites):
        exceptions = []
        for testsuite in testsuites:
            try:
                sys.path.insert(0, os.path.dirname(testsuite))
                spec = importlib.util.spec_from_file_location(os.path.basename(testsuite).replace(".py", ""), testsuite)
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                return (module, None)
            except Exception as e:
                exceptions.append("Could not load %s: %s" % (testsuite, e))
                continue
            finally:
                sys.path.remove(os.path.dirname(testsuite))

        return (None, exceptions)

    def _load_testsuites(self):
        testsuites = {}
        for testsuite in self.options.testsuites:
            if testsuite.endswith('.py') and os.path.exists(testsuite):
                testsuite = os.path.abspath(os.path.expanduser(testsuite))
                loaded_module = self._load_testsuite([testsuite])
            else:
                possible_testsuites_paths = [os.path.join(d, testsuite + ".py")
                                             for d in self.options.testsuites_dirs]
                loaded_module = self._load_testsuite(possible_testsuites_paths)

            module = loaded_module[0]
            if not loaded_module[0]:
                if "." in testsuite:
                    self.options.testsuites.append(testsuite.split('.')[0])
                    self.info("%s looks like a test name, trying that" %
                              testsuite)
                    self.options.wanted_tests.append(testsuite)
                else:
                    if testsuite in testsuites:
                        self.info('Testuite %s was loaded previously', testsuite)
                        continue
                    printc("Could not load testsuite: %s, reasons: %s" % (
                        testsuite, loaded_module[1]), Colors.FAIL)
                continue

            if module.__name__ in testsuites:
                self.info("Trying to load testsuite '%s' a second time?", module.__name__)
                continue

            testsuites[module.__name__] = module
            if not hasattr(module, "TEST_MANAGER"):
                module.TEST_MANAGER = [tester.name for tester in self.testers]
            elif not isinstance(module.TEST_MANAGER, list):
                module.TEST_MANAGER = [module.TEST_MANAGER]

        self.options.testsuites = list(testsuites.values())

    def _setup_testsuites(self):
        for testsuite in self.options.testsuites:
            loaded = False
            wanted_test_manager = None
            # TEST_MANAGER has been set in _load_testsuites()
            assert hasattr(testsuite, "TEST_MANAGER")
            wanted_test_manager = testsuite.TEST_MANAGER
            if not isinstance(wanted_test_manager, list):
                wanted_test_manager = [wanted_test_manager]

            for tester in self.testers:
                if wanted_test_manager is not None and \
                        tester.name not in wanted_test_manager:
                    continue

                prev_testsuite_name = TestsManager.loading_testsuite
                if self.options.user_paths:
                    TestsManager.loading_testsuite = tester.name
                    tester.register_defaults()
                    loaded = True
                else:
                    TestsManager.loading_testsuite = testsuite.__name__
                    if testsuite.setup_tests(tester, self.options):
                        loaded = True
                if prev_testsuite_name:
                    TestsManager.loading_testsuite = prev_testsuite_name

            if not loaded:
                printc("Could not load testsuite: %s"
                       " maybe because of missing TestManager"
                       % (testsuite), Colors.FAIL)
                return False

    def _load_config(self, options):
        printc("Loading config files is DEPRECATED"
               " you should use the new testsuite format now",)

        for tester in self.testers:
            tester.options = options
            globals()[tester.name] = tester
        globals()["options"] = options
        c__file__ = __file__
        globals()["__file__"] = self.options.config
        exec(compile(open(self.options.config).read(),
                     self.options.config, 'exec'), globals())
        globals()["__file__"] = c__file__

    def set_settings(self, options, args):
        if options.xunit_file:
            self.reporter = reporters.XunitReporter(options)
        else:
            self.reporter = reporters.Reporter(options)

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
        if not self.options.testsuites:
            printc("Not testsuite loaded!", Colors.FAIL)
            return False

        for tester in self.testers:
            tester.set_settings(options, args, self.reporter)

        if not options.config and options.testsuites:
            if self._setup_testsuites() is False:
                return False

        if self.options.check_bugs_status:
            printc("-> Checking bugs resolution... ", end='')

        for tester in self.testers:
            if not tester.check_blacklists():
                return False

            tester.log_blacklists()

            if not tester.check_expected_issues():
                return False

        if self.options.check_bugs_status:
            printc("OK", Colors.OKGREEN)

        if self.needs_http_server() or options.httponly is True:
            self.httpsrv = HTTPServer(options)
            self.httpsrv.start()

        if options.no_display:
            self.vfb_server = get_virual_frame_buffer_server(options)
            res = self.vfb_server.start()
            if res[0] is False:
                printc("Could not start virtual frame server: %s" % res[1],
                       Colors.FAIL)
                return False
            os.environ["DISPLAY"] = self.vfb_server.display_id

        return True

    def _check_tester_has_other_testsuite(self, testsuite, tester):
        if tester.name != testsuite.TEST_MANAGER[0]:
            return True

        for t in self.options.testsuites:
            if t != testsuite:
                for other_testmanager in t.TEST_MANAGER:
                    if other_testmanager == tester.name:
                        return True

        return False

    def _check_defined_tests(self, tester, tests):
        if self.options.blacklisted_tests or self.options.wanted_tests:
            return

        tests_names = [test.classname for test in tests]
        testlist_changed = False
        for testsuite in self.options.testsuites:
            if not self._check_tester_has_other_testsuite(testsuite, tester) \
                    and tester.check_testslist:
                try:
                    testlist_file = open(os.path.splitext(testsuite.__file__)[0] + ".testslist",
                                         'r+')

                    know_tests = testlist_file.read().split("\n")
                    testlist_file.close()

                    testlist_file = open(os.path.splitext(testsuite.__file__)[0] + ".testslist",
                                         'w')
                except IOError:
                    continue

                optional_out = []
                for test in know_tests:
                    if test and test.strip('~') not in tests_names:
                        if not test.startswith('~'):
                            testlist_changed = True
                            printc("Test %s Not in testsuite %s anymore"
                                   % (test, testsuite.__file__), Colors.FAIL)
                        else:
                            optional_out.append((test, None))

                tests_names = sorted([(test.classname, test) for test in tests] + optional_out,
                                     key=lambda x: x[0].strip('~'))

                for tname, test in tests_names:
                    if test and test.optional:
                        tname = '~' + tname
                    testlist_file.write("%s\n" % (tname))
                    if tname and tname not in know_tests:
                        printc("Test %s is NEW in testsuite %s"
                               % (tname, testsuite.__file__),
                               Colors.FAIL if self.options.fail_on_testlist_change else Colors.OKGREEN)
                        testlist_changed = True

                testlist_file.close()
                break

        return testlist_changed

    def _split_tests(self, num_groups):
        groups = [[] for x in range(num_groups)]
        group = cycle(groups)
        for test in self.tests:
            next(group).append(test)
        return groups

    def list_tests(self):
        for tester in self.testers:
            if not self._tester_needed(tester):
                continue

            tests = tester.list_tests()
            if self._check_defined_tests(tester, tests) and \
                    self.options.fail_on_testlist_change:
                raise RuntimeError("Unexpected new test in testsuite.")

            self.tests.extend(tests)
        self.tests.sort(key=lambda test: test.classname)

        if self.options.num_parts < 1:
            raise RuntimeError("Tests must be split in positive number of parts.")
        if self.options.num_parts > len(self.tests):
            raise RuntimeError("Cannot have more parts then there exist tests.")
        if self.options.part_index < 1 or self.options.part_index > self.options.num_parts:
            raise RuntimeError("Part index is out of range")

        self.tests = self._split_tests(self.options.num_parts)[self.options.part_index - 1]
        return self.tests

    def _tester_needed(self, tester):
        for testsuite in self.options.testsuites:
            if tester.name in testsuite.TEST_MANAGER:
                return True
        return False

    def server_wrapper(self, ready):
        self.server = GstValidateTCPServer(
            ('localhost', 0), GstValidateListener)
        self.server.socket.settimeout(None)
        self.server.launcher = self
        self.serverport = self.server.socket.getsockname()[1]
        self.info("%s server port: %s" % (self, self.serverport))
        ready.set()

        self.server.serve_forever(poll_interval=0.05)

    def _start_server(self):
        self.info("Starting TCP Server")
        ready = threading.Event()
        self.server_thread = threading.Thread(target=self.server_wrapper,
                                              kwargs={'ready': ready})
        self.server_thread.start()
        ready.wait()
        os.environ["GST_VALIDATE_SERVER"] = "tcp://localhost:%s" % self.serverport

    def _stop_server(self):
        if self.server:
            self.server.shutdown()
            self.server_thread.join()
            self.server.server_close()
            self.server = None

    def test_wait(self):
        while True:
            # Check process every second for timeout
            try:
                self.queue.get(timeout=1)
            except queue.Empty:
                pass

            for test in self.jobs:
                if test.process_update():
                    self.jobs.remove(test)
                    return test

    def tests_wait(self):
        try:
            test = self.test_wait()
            test.check_results()
        except KeyboardInterrupt:
            for test in self.jobs:
                test.kill_subprocess()
            raise

        return test

    def start_new_job(self, tests_left):
        try:
            test = tests_left.pop(0)
        except IndexError:
            return False

        test.test_start(self.queue)

        self.jobs.append(test)

        return True

    def print_result(self, current_test_num, test, total_num_tests, retry_on_failures=False):
        if test.result not in [Result.PASSED, Result.KNOWN_ERROR] and (not retry_on_failures or test.max_retries):
            printc(str(test), color=utils.get_color_for_result(test.result))

        length = 80
        progress = int(length * current_test_num // total_num_tests)
        bar = '█' * progress + '-' * (length - progress)
        if is_tty():
            printc('\r|%s| [%s/%s]' % (bar, current_test_num, total_num_tests), end='\r')
        else:
            if progress > self.current_progress:
                self.current_progress = progress
                printc('|%s| [%s/%s]' % (bar, current_test_num, total_num_tests))

    def _run_tests(self, running_tests=None, all_alone=False, retry_on_failures=False, total_num_tests=None):
        if not self.all_tests:
            self.all_tests = self.list_tests()

        if not running_tests:
            running_tests = self.tests

        self.reporter.init_timer()
        alone_tests = []
        tests = []
        for test in running_tests:
            if test.is_parallel and not all_alone:
                tests.append(test)
            else:
                alone_tests.append(test)

        # use max to defend against the case where all tests are alone_tests
        max_num_jobs = max(min(self.options.num_jobs, len(tests)), 1)
        jobs_running = 0

        if self.options.forever and len(tests) < self.options.num_jobs and len(tests):
            max_num_jobs = self.options.num_jobs
            copied = []
            i = 0
            while (len(tests) + len(copied)) < max_num_jobs:
                copied.append(tests[i].copy(len(copied) + 1))

                i += 1
                if i >= len(tests):
                    i = 0
            tests += copied
            self.tests += copied

        self.total_num_tests = len(self.all_tests)
        prefix = "=> Re-r" if total_num_tests else "R"
        total_num_tests = total_num_tests if total_num_tests else self.total_num_tests
        printc(f"\n{prefix}unning {total_num_tests} tests...", color=Colors.HEADER)
        # if order of test execution doesn't matter, shuffle
        # the order to optimize cpu usage
        if self.options.shuffle:
            random.shuffle(tests)
            random.shuffle(alone_tests)

        current_test_num = 1
        to_retry = []
        for num_jobs, tests in [(max_num_jobs, tests), (1, alone_tests)]:
            tests_left = list(tests)
            for i in range(num_jobs):
                if not self.start_new_job(tests_left):
                    break
                jobs_running += 1

            while jobs_running != 0:
                test = self.tests_wait()
                jobs_running -= 1
                current_test_num += 1
                res = test.test_end(retry_on_failures=retry_on_failures)
                to_report = True
                if res not in [Result.PASSED, Result.SKIPPED, Result.KNOWN_ERROR]:
                    if self.options.forever or self.options.fatal_error:
                        self.print_result(current_test_num - 1, test, retry_on_failures=retry_on_failures,
                            total_num_tests=total_num_tests)
                        self.reporter.after_test(test)
                        return False

                    if retry_on_failures or test.max_retries and not self.options.no_retry_on_failures:
                        if not self.options.redirect_logs:
                            test.copy_logfiles()
                        to_retry.append(test)

                        # Not adding to final report if flakiness is tolerated
                        if test.max_retries:
                            test.max_retries -= 1
                            to_report = False
                self.print_result(current_test_num - 1, test,
                    retry_on_failures=retry_on_failures,
                    total_num_tests=total_num_tests)
                if to_report:
                    self.reporter.after_test(test)
                if self.start_new_job(tests_left):
                    jobs_running += 1

        if to_retry:
            printc("--> Rerunning the following tests to see if they are flaky:", Colors.WARNING)
            for test in to_retry:
                test.clean()
                printc(f'  * {test.classname}')
            printc('')
            self.current_progress = -1
            res = self._run_tests(
                to_retry,
                all_alone=True,
                retry_on_failures=False,
                total_num_tests=len(to_retry),
            )

            return res

        return True

    def clean_tests(self, stop_server=False):
        for test in self.tests:
            test.clean()
        if stop_server:
            self._stop_server()

    def run_tests(self):
        r = 0
        try:
            self._start_server()
            if self.options.forever:
                r = 1
                while True:
                    self.current_progress = -1
                    printc("-> Iteration %d" % r, end='\r')

                    if not self._run_tests():
                        break
                    r += 1
                    self.clean_tests()
                    msg = "-> Iteration %d... %sOK%s" % (r, Colors.OKGREEN, Colors.ENDC)
                    printc(msg, end="\r")

                return False
            elif self.options.n_runs:
                res = True
                for r in range(self.options.n_runs):
                    self.current_progress = -1
                    printc("-> Iteration %d" % r, end='\r')
                    if not self._run_tests(retry_on_failures=self.options.retry_on_failures):
                        res = False
                        printc("ERROR", Colors.FAIL, end="\r")
                    else:
                        printc("OK", Colors.OKGREEN, end="\r")
                    self.clean_tests()

                return res
            else:
                return self._run_tests(retry_on_failures=self.options.retry_on_failures)
        finally:
            if self.options.forever:
                printc("\n-> Ran %d times" % r)
            if self.httpsrv:
                self.httpsrv.stop()
            if self.vfb_server:
                self.vfb_server.stop()
            self.clean_tests(True)

    def final_report(self):
        return self.reporter.final_report()

    def needs_http_server(self):
        for tester in self.testers:
            if tester.needs_http_server():
                return True


class NamedDic(object):

    def __init__(self, props):
        if props:
            for name, value in props.items():
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

    def needs_live_content(self):
        # Scenarios that can only be used on live content
        if hasattr(self, "live_content_required"):
            return bool(self.live_content_required)
        return False

    def compatible_with_live_content(self):
        # if a live content is required it's implicitly compatible with
        # live content
        if self.needs_live_content():
            return True
        if hasattr(self, "live_content_compatible"):
            return bool(self.live_content_compatible)
        return False

    def get_min_media_duration(self):
        if hasattr(self, "min_media_duration"):
            return float(self.min_media_duration)

        return 0

    def does_reverse_playback(self):
        if hasattr(self, "reverse_playback"):
            return bool(self.reverse_playback)

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

    def __repr__(self):
        return "<Scenario %s>" % self.name


class ScenarioManager(Loggable):
    _instance = None
    system_scenarios = []
    special_scenarios = {}

    FILE_EXTENSION = "scenario"

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
            if re.findall(r'%s\..*\.%s$' % (re.escape(mfile_bname), self.FILE_EXTENSION), f):
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
        log_path = os.path.join(self.config.logsdir, "scenarios_discovery.log")
        logs = open(log_path, 'w')

        try:
            command = [GstValidateBaseTestManager.COMMAND,
                       "--scenarios-defs-output-file", scenario_defs]
            command.extend(scenario_paths)
            subprocess.check_call(command, stdout=logs, stderr=logs)
        except subprocess.CalledProcessError as e:
            self.error(e)
            self.error('See %s' % log_path)
            pass

        config = configparser.RawConfigParser()
        f = open(scenario_defs)
        config.read_file(f)

        for section in config.sections():
            name = None
            if scenario_paths:
                for scenario_path in scenario_paths:
                    if section == scenario_path:
                        if mfile is None:
                            name = os.path.basename(section).replace("." + self.FILE_EXTENSION, "")
                            path = scenario_path
                        else:
                            # The real name of the scenario is:
                            # filename.REALNAME.scenario
                            name = scenario_path.replace(mfile + ".", "").replace(
                                "." + self.FILE_EXTENSION, "")
                            path = scenario_path
                        break
            else:
                name = os.path.basename(section).replace("." + self.FILE_EXTENSION, "")
                path = None

            assert name

            props = config.items(section)
            scenario = Scenario(name, props, path)
            if scenario_paths:
                self.special_scenarios[path] = scenario
            scenarios.append(scenario)

        if not scenario_paths:
            self.discovered = True
            self.system_scenarios.extend(scenarios)

        return scenarios

    def get_scenario(self, name):
        if name is not None and os.path.isabs(name) and name.endswith(self.FILE_EXTENSION):
            scenario = self.special_scenarios.get(name)
            if scenario:
                return scenario

            scenarios = self.discover_scenarios([name])
            self.special_scenarios[name] = scenarios

            if scenarios:
                return scenarios[0]

        if self.discovered is False:
            self.discover_scenarios()

        if name is None:
            return self.system_scenarios

        try:
            return [scenario for scenario in self.system_scenarios if scenario.name == name][0]
        except IndexError:
            self.warning("Scenario: %s not found" % name)
            return None


class GstValidateBaseTestManager(TestsManager):
    scenarios_manager = ScenarioManager()
    features_cache = {}

    def __init__(self):
        super(GstValidateBaseTestManager, self).__init__()
        self._scenarios = []
        self._encoding_formats = []

    @classmethod
    def update_commands(cls, extra_paths=None):
        for varname, cmd in {'': 'gst-validate',
                             'TRANSCODING_': 'gst-validate-transcoding',
                             'MEDIA_CHECK_': 'gst-validate-media-check',
                             'RTSP_SERVER_': 'gst-validate-rtsp-server',
                             'INSPECT_': 'gst-inspect'}.items():
            setattr(cls, varname + 'COMMAND', which(cmd + '-1.0', extra_paths))

    @classmethod
    def has_feature(cls, featurename):
        try:
            return cls.features_cache[featurename]
        except KeyError:
            pass

        try:
            subprocess.check_output([cls.INSPECT_COMMAND, featurename])
            res = True
        except subprocess.CalledProcessError:
            res = False

        cls.features_cache[featurename] = res
        return res

    def add_scenarios(self, scenarios):
        """
        @scenarios A list or a unic scenario name(s) to be run on the tests.
                    They are just the default scenarios, and then depending on
                    the TestsGenerator to be used you can have more fine grained
                    control on what to be run on each series of tests.
        """
        if isinstance(scenarios, list):
            self._scenarios.extend(scenarios)
        else:
            self._scenarios.append(scenarios)

        self._scenarios = list(set(self._scenarios))

    def set_scenarios(self, scenarios):
        """
        Override the scenarios
        """
        self._scenarios = []
        self.add_scenarios(scenarios)

    def get_scenarios(self):
        return self._scenarios

    def add_encoding_formats(self, encoding_formats):
        """
        :param encoding_formats: A list or one single #MediaFormatCombinations describing wanted output
                           formats for transcoding test.
                           They are just the default encoding formats, and then depending on
                           the TestsGenerator to be used you can have more fine grained
                           control on what to be run on each series of tests.
        """
        if isinstance(encoding_formats, list):
            self._encoding_formats.extend(encoding_formats)
        else:
            self._encoding_formats.append(encoding_formats)

        self._encoding_formats = list(set(self._encoding_formats))

    def get_encoding_formats(self):
        return self._encoding_formats


GstValidateBaseTestManager.update_commands()


class MediaDescriptor(Loggable):

    def __init__(self):
        Loggable.__init__(self)

    def get_path(self):
        raise NotImplemented

    def has_frames(self):
        return False

    def get_framerate(self):
        for ttype, caps_str in self.get_tracks_caps():
            if ttype != "video":
                continue

            caps = utils.GstCaps.new_from_str(caps_str)
            if not caps:
                self.warning("Could not create caps for %s" % caps_str)
                continue

            framerate = caps[0].get("framerate")
            if framerate:
                return framerate

        return Fraction(0, 1)

    def get_media_filepath(self):
        raise NotImplemented

    def skip_parsers(self):
        return False

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

    def is_live(self):
        raise NotImplemented

    def is_image(self):
        raise NotImplemented

    def get_num_tracks(self, track_type):
        raise NotImplemented

    def get_tracks_caps(self):
        return []

    def can_play_reverse(self):
        raise NotImplemented

    def prerrols(self):
        return True

    def is_compatible(self, scenario):
        if scenario is None:
            return True

        if scenario.seeks() and (not self.is_seekable() or self.is_image()):
            self.debug("Do not run %s as %s does not support seeking",
                       scenario, self.get_uri())
            return False

        if self.is_image() and scenario.needs_clock_sync():
            self.debug("Do not run %s as %s is an image",
                       scenario, self.get_uri())
            return False

        if not self.can_play_reverse() and scenario.does_reverse_playback():
            return False

        if not self.is_live() and scenario.needs_live_content():
            self.debug("Do not run %s as %s is not a live content",
                       scenario, self.get_uri())
            return False

        if self.is_live() and not scenario.compatible_with_live_content():
            self.debug("Do not run %s as %s is a live content",
                       scenario, self.get_uri())
            return False

        if not self.prerrols() and getattr(scenario, 'needs_preroll', False):
            return False

        if self.get_duration() and self.get_duration() / GST_SECOND < scenario.get_min_media_duration():
            self.debug(
                "Do not run %s as %s is too short (%i < min media duation : %i",
                scenario, self.get_uri(),
                self.get_duration() / GST_SECOND,
                scenario.get_min_media_duration())
            return False

        for track_type in ['audio', 'subtitle', 'video']:
            if self.get_num_tracks(track_type) < scenario.get_min_tracks(track_type):
                self.debug("%s -- %s | At least %s %s track needed  < %s"
                           % (scenario, self.get_uri(), track_type,
                              scenario.get_min_tracks(track_type),
                              self.get_num_tracks(track_type)))
                return False

        return True


class GstValidateMediaDescriptor(MediaDescriptor):
    # Some extension file for discovering results
    SKIPPED_MEDIA_INFO_EXT = "media_info.skipped"
    MEDIA_INFO_EXT = "media_info"
    PUSH_MEDIA_INFO_EXT = "media_info.push"
    STREAM_INFO_EXT = "stream_info"

    __all_descriptors = {}

    @classmethod
    def get(cls, xml_path):
        if xml_path in cls.__all_descriptors:
            return cls.__all_descriptors[xml_path]
        return GstValidateMediaDescriptor(xml_path)

    def __init__(self, xml_path):
        super(GstValidateMediaDescriptor, self).__init__()

        self._media_file_path = None
        main_descriptor = self.__all_descriptors.get(xml_path)
        if main_descriptor:
            self._copy_data_from_main(main_descriptor)
        else:
            self.__all_descriptors[xml_path] = self

            self._xml_path = xml_path
            try:
                media_xml = ET.parse(xml_path).getroot()
            except xml.etree.ElementTree.ParseError:
                printc("Could not parse %s" % xml_path,
                    Colors.FAIL)
                raise
            self._extract_data(media_xml)

        self.set_protocol(urllib.parse.urlparse(self.get_uri()).scheme)

    def skip_parsers(self):
        return self._skip_parsers

    def has_frames(self):
        return self._has_frames

    def _copy_data_from_main(self, main_descriptor):
        for attr in main_descriptor.__dict__.keys():
            setattr(self, attr, getattr(main_descriptor, attr))

    def _extract_data(self, media_xml):
        # Extract the information we need from the xml
        self._caps = media_xml.findall("streams")[0].attrib["caps"]
        self._track_caps = []
        try:
            streams = media_xml.findall("streams")[0].findall("stream")
        except IndexError:
            pass
        else:
            for stream in streams:
                self._track_caps.append(
                    (stream.attrib["type"], stream.attrib["caps"]))

        self._skip_parsers = bool(int(media_xml.attrib.get('skip-parsers', 0)))
        self._has_frames = bool(int(media_xml.attrib["frame-detection"]))
        self._duration = int(media_xml.attrib["duration"])
        self._uri = media_xml.attrib["uri"]
        parsed_uri = urllib.parse.urlparse(self.get_uri())
        self._protocol = media_xml.get("protocol", parsed_uri.scheme)
        if parsed_uri.scheme == "file":
            if not os.path.exists(parsed_uri.path) and os.path.exists(self.get_media_filepath()):
                self._uri = "file://" + self.get_media_filepath()
        elif parsed_uri.scheme == Protocols.IMAGESEQUENCE:
            self._media_file_path = os.path.join(os.path.dirname(self.__cleanup_media_info_ext()), os.path.basename(parsed_uri.path))
            self._uri = parsed_uri._replace(path=os.path.join(os.path.dirname(self.__cleanup_media_info_ext()), os.path.basename(self._media_file_path))).geturl()
        self._is_seekable = media_xml.attrib["seekable"].lower() == "true"
        self._is_live = media_xml.get("live", "false").lower() == "true"
        self._is_image = False
        for stream in media_xml.findall("streams")[0].findall("stream"):
            if stream.attrib["type"] == "image":
                self._is_image = True
        self._track_types = []
        for stream in media_xml.findall("streams")[0].findall("stream"):
            self._track_types.append(stream.attrib["type"])

    def __cleanup_media_info_ext(self):
        for ext in [self.MEDIA_INFO_EXT, self.PUSH_MEDIA_INFO_EXT, self.STREAM_INFO_EXT,
                self.SKIPPED_MEDIA_INFO_EXT, ]:
            if self._xml_path.endswith(ext):
                return self._xml_path[:len(self._xml_path) - (len(ext) + 1)]

        assert "Not reached" == None  # noqa

    @staticmethod
    def new_from_uri(uri, verbose=False, include_frames=False, is_push=False, is_skipped=False):
        """
            include_frames = 0 # Never
            include_frames = 1 # always
            include_frames = 2 # if previous file included them

        """
        media_path = utils.url2path(uri)

        ext = GstValidateMediaDescriptor.MEDIA_INFO_EXT
        if is_push:
            ext = GstValidateMediaDescriptor.PUSH_MEDIA_INFO_EXT
        elif is_skipped:
            ext = GstValidateMediaDescriptor.SKIPPED_MEDIA_INFO_EXT
        descriptor_path = "%s.%s" % (media_path, ext)
        args = GstValidateBaseTestManager.MEDIA_CHECK_COMMAND.split(" ")
        if include_frames == 2:
            try:
                media_xml = ET.parse(descriptor_path).getroot()
                prev_uri = urllib.parse.urlparse(media_xml.attrib['uri'])
                if prev_uri.scheme == Protocols.IMAGESEQUENCE:
                    parsed_uri = urllib.parse.urlparse(uri)
                    uri = prev_uri._replace(path=os.path.join(os.path.dirname(parsed_uri.path), os.path.basename(prev_uri.path))).geturl()
                include_frames = bool(int(media_xml.attrib["frame-detection"]))
                if bool(int(media_xml.attrib.get("skip-parsers", 0))):
                    args.append("--skip-parsers")
            except FileNotFoundError:
                pass
        else:
            include_frames = bool(include_frames)
        args.append(uri)

        args.extend(["--output-file", descriptor_path])
        if include_frames:
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
                loggable.warning("GstValidateMediaDescriptor",
                                 "Exception: %s" % e)
            return None

        if verbose:
            printc("Result: Passed", Colors.OKGREEN)

        try:
            return GstValidateMediaDescriptor(descriptor_path)
        except (IOError, xml.etree.ElementTree.ParseError):
            return None

    def get_path(self):
        return self._xml_path

    def need_clock_sync(self):
        return Protocols.needs_clock_sync(self.get_protocol())

    def get_media_filepath(self):
        if self._media_file_path is None:
            self._media_file_path = self.__cleanup_media_info_ext()
        return self._media_file_path

    def get_caps(self):
        return self._caps

    def get_tracks_caps(self):
        return self._track_caps

    def get_uri(self):
        return self._uri

    def get_duration(self):
        return self._duration

    def set_protocol(self, protocol):
        if self._xml_path.endswith(GstValidateMediaDescriptor.PUSH_MEDIA_INFO_EXT):
            self._protocol = Protocols.PUSHFILE
        else:
            self._protocol = protocol

    def get_protocol(self):
        return self._protocol

    def is_seekable(self):
        return self._is_seekable

    def is_live(self):
        return self._is_live

    def can_play_reverse(self):
        return True

    def is_image(self):
        return self._is_image

    def get_num_tracks(self, track_type):
        n = 0
        for t in self._track_types:
            if t == track_type:
                n += 1

        return n

    def get_clean_name(self):
        name = os.path.basename(self.get_path())
        regex = '|'.join(['\\.%s$' % ext for ext in [self.SKIPPED_MEDIA_INFO_EXT, self.MEDIA_INFO_EXT, self.PUSH_MEDIA_INFO_EXT, self.STREAM_INFO_EXT]])
        name = re.sub(regex, "", name)

        return name.replace('.', "_")


class MediaFormatCombination(object):
    FORMATS = {"aac": "audio/mpeg,mpegversion=4",  # Audio
               "ac3": "audio/x-ac3",
               "vorbis": "audio/x-vorbis",
               "mp3": "audio/mpeg,mpegversion=1,layer=3",
               "opus": "audio/x-opus",
               "rawaudio": "audio/x-raw",

               # Video
               "h264": "video/x-h264",
               "h265": "video/x-h265",
               "vp8": "video/x-vp8",
               "vp9": "video/x-vp9",
               "theora": "video/x-theora",
               "prores": "video/x-prores",
               "jpeg": "image/jpeg",

               # Containers
               "webm": "video/webm",
               "ogg": "application/ogg",
               "mkv": "video/x-matroska",
               "mp4": "video/quicktime,variant=iso;",
               "quicktime": "video/quicktime;"}

    def __str__(self):
        return "%s and %s in %s" % (self.audio, self.video, self.container)

    def __init__(self, container, audio, video, duration_factor=1,
            video_restriction=None, audio_restriction=None):
        """
        Describes a media format to be used for transcoding tests.

        :param container: A string defining the container format to be used, must bin in self.FORMATS
        :param audio: A string defining the audio format to be used, must bin in self.FORMATS
        :param video: A string defining the video format to be used, must bin in self.FORMATS
        """
        self.container = container
        self.audio = audio
        self.video = video
        self.video_restriction = video_restriction
        self.audio_restriction = audio_restriction

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
