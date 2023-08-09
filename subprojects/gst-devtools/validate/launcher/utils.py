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
""" Some utilies. """

try:
    import config
except ImportError:
    from . import config

import json
import numbers
import os
import platform
import re
import shutil
import shlex
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request
import urllib.error
import urllib.parse

from .loggable import Loggable, warning
from operator import itemgetter
from xml.etree import ElementTree
from collections import defaultdict
from fractions import Fraction


GST_SECOND = int(1000000000)
DEFAULT_TIMEOUT = 30

DEFAULT_MAIN_DIR = os.path.join(config.BUILDDIR, "subprojects", "gst-integration-testsuites")
DEFAULT_GST_QA_ASSETS = os.path.join(config.SRCDIR, "subprojects", "gst-integration-testsuites")
USING_SUBPROJECT = os.path.exists(os.path.join(config.BUILDDIR, "subprojects", "gst-integration-testsuites"))
if not USING_SUBPROJECT:
    DEFAULT_MAIN_DIR = os.path.join(os.path.expanduser("~"), "gst-validate")
    DEFAULT_GST_QA_ASSETS = os.path.join(DEFAULT_MAIN_DIR, "gstreamer", "subprojects", "gst-integration-testsuites")

DEFAULT_MAIN_DIR = os.environ.get('GST_VALIDATE_LAUNCHER_MAIN_DIR', DEFAULT_MAIN_DIR)
DEFAULT_TESTSUITES_DIRS = [os.path.join(DEFAULT_GST_QA_ASSETS, "testsuites")]


DISCOVERER_COMMAND = "gst-discoverer-1.0"
# Use to set the duration from which a test is considered as being 'long'
LONG_TEST = 40


class Result(object):
    NOT_RUN = "Not run"
    FAILED = "Failed"
    TIMEOUT = "Timeout"
    PASSED = "Passed"
    SKIPPED = "Skipped"
    KNOWN_ERROR = "Known error"


class Protocols(object):
    HTTP = "http"
    FILE = "file"
    PUSHFILE = "pushfile"
    HLS = "hls"
    DASH = "dash"
    RTSP = "rtsp"
    IMAGESEQUENCE = "imagesequence"

    @staticmethod
    def needs_clock_sync(protocol):
        if protocol in [Protocols.HLS, Protocols.DASH]:
            return True

        return False


def is_tty():
    return hasattr(sys.stdout, 'isatty') and sys.stdout.isatty()


def supports_ansi_colors():
    if 'GST_VALIDATE_LAUNCHER_FORCE_COLORS' in os.environ:
        return True

    platform = sys.platform
    supported_platform = platform != 'win32' or 'ANSICON' in os.environ
    if not supported_platform or not is_tty():
        return False
    return True


class Colors(object):
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'


def desactivate_colors():
    Colors.HEADER = ''
    Colors.OKBLUE = ''
    Colors.OKGREEN = ''
    Colors.WARNING = ''
    Colors.FAIL = ''
    Colors.ENDC = ''


if not supports_ansi_colors():
    desactivate_colors()


def mkdir(directory):
    try:
        os.makedirs(directory)
    except os.error:
        pass


def which(name, extra_path=None):
    exts = [_f for _f in os.environ.get('PATHEXT', '').split(os.pathsep) if _f]
    path = os.environ.get('PATH', '')
    if extra_path:
        path = extra_path + os.pathsep + path
    if not path:
        return []

    for p in path.split(os.pathsep):
        p = os.path.join(p, name)
        if os.access(p, os.X_OK):
            return p
        for e in exts:
            pext = p + e
            if os.access(pext, os.X_OK):
                return pext
    return None


def get_color_for_result(result):
    if result is Result.FAILED:
        color = Colors.FAIL
    elif result is Result.TIMEOUT:
        color = Colors.WARNING
    elif result is Result.PASSED:
        color = Colors.OKGREEN
    else:
        color = Colors.OKBLUE

    return color


last_carriage_return_len = 0


def printc(message, color="", title=False, title_char='', end="\n"):
    global last_carriage_return_len
    if title or title_char:
        length = 0
        for line in message.split("\n"):
            if len(line) > length:
                length = len(line)
        if length == 0:
            length = len(message)

        needed_spaces = ' ' * max(0, last_carriage_return_len - length)
        if title is True:
            message = length * "=" + needed_spaces + "\n" \
                + str(message) + "\n" + length * '='
        else:
            message = str(message) + needed_spaces + "\n" + \
                length * title_char

    if hasattr(message, "result") and color == '':
        color = get_color_for_result(message.result)

    if not is_tty():
        end = "\n"

    message = str(message)
    message += ' ' * max(0, last_carriage_return_len - len(message))
    if end == '\r':
        term_width = shutil.get_terminal_size((80, 20))[0]
        if len(message) > term_width:
            message = message[0:term_width - 2] + '…'
        last_carriage_return_len = len(message)
    else:
        last_carriage_return_len = 0
    sys.stdout.write(color + str(message) + Colors.ENDC + end)
    sys.stdout.flush()


def launch_command(command, color=None, fails=False):
    printc(command, Colors.OKGREEN, True)
    res = os.system(command)
    if res != 0 and fails is True:
        raise subprocess.CalledProcessError(res, "%s failed" % command)


def path2url(path):
    return urllib.parse.urljoin('file:', urllib.request.pathname2url(path))


def is_windows():
    platname = platform.system().lower()
    return platname == 'windows' or 'mingw' in platname


def url2path(url):
    path = urllib.parse.urlparse(url).path
    if "win32" in sys.platform:
        if path[0] == '/':
            return path[1:]  # We need to remove the first '/' on windows
    path = urllib.parse.unquote(path)
    return path


def isuri(string):
    url = urllib.parse.urlparse(string)
    if url.scheme != "" and url.scheme != "":
        return True

    return False


def touch(fname, times=None):
    with open(fname, 'a'):
        os.utime(fname, times)


def get_subclasses(klass, env):
    subclasses = []
    for symb in env.items():
        try:
            if issubclass(symb[1], klass) and not symb[1] is klass:
                subclasses.append(symb[1])
        except TypeError:
            pass

    return subclasses


def TIME_ARGS(time):
    return "%u:%02u:%02u.%09u" % (time / (GST_SECOND * 60 * 60),
                                  (time / (GST_SECOND * 60)) % 60,
                                  (time / GST_SECOND) % 60,
                                  time % GST_SECOND)


def look_for_file_in_source_dir(subdir, name):
    root_dir = os.path.abspath(os.path.dirname(
        os.path.join(os.path.dirname(os.path.abspath(__file__)))))
    p = os.path.join(root_dir, subdir, name)
    if os.path.exists(p):
        return p

    return None


# Returns the path $top_src_dir/@subdir/@name if running from source, or
# $DATADIR/gstreamer-1.0/validate/@name if not
def get_data_file(subdir, name):
    # Are we running from sources?
    p = look_for_file_in_source_dir(subdir, name)
    if p:
        return p

    # Look in system data dirs
    p = os.path.join(config.DATADIR, 'gstreamer-1.0', 'validate', name)
    if os.path.exists(p):
        return p

    return None

#
# Some utilities to parse gst-validate output   #
#


def gsttime_from_tuple(stime):
    return int((int(stime[0]) * 3600 + int(stime[1]) * 60 + int(stime[2])) * GST_SECOND + int(stime[3]))


timeregex = re.compile(r'(?P<_0>.+):(?P<_1>.+):(?P<_2>.+)\.(?P<_3>.+)')


def parse_gsttimeargs(time):
    stime = list(map(itemgetter(1), sorted(
        timeregex.match(time).groupdict().items())))
    return int((int(stime[0]) * 3600 + int(stime[1]) * 60 + int(stime[2])) * GST_SECOND + int(stime[3]))


def get_duration(media_file):

    duration = 0
    res = ''
    try:
        res = subprocess.check_output(
            [DISCOVERER_COMMAND, media_file]).decode()
    except subprocess.CalledProcessError:
        # gst-media-check returns !0 if seeking is not possible, we do not care
        # in that case.
        pass

    for line in res.split('\n'):
        if "Duration: " in line:
            duration = parse_gsttimeargs(line.replace("Duration: ", ""))
            break

    return duration


def get_scenarios():
    GST_VALIDATE_COMMAND = "gst-validate-1.0"
    os.system("%s --scenarios-defs-output-file %s" % (GST_VALIDATE_COMMAND,
                                                      ))


def get_gst_build_valgrind_suppressions():
    if hasattr(get_gst_build_valgrind_suppressions, "data"):
        return get_gst_build_valgrind_suppressions.data

    get_gst_build_valgrind_suppressions.data = []
    if not os.path.exists(os.path.join(config.SRCDIR, "subprojects")):
        return get_gst_build_valgrind_suppressions.data

    for suppression_path in ["gstreamer/tests/check/gstreamer.supp",
                             "gst-plugins-base/tests/check/gst-plugins-base.supp",
                             "gst-plugins-good/tests/check/gst-plugins-good.supp",
                             "gst-plugins-bad/tests/check/gst-plugins-bad.supp",
                             "gst-plugins-ugly/tests/check/gst-plugins-ugly.supp",
                             "gst-libav/tests/check/gst-libav.supp",
                             "gst-devtools/validate/data/gstvalidate.supp",
                             "libnice/tests/libnice.supp",
                             "libsoup/tests/libsoup.supp",
                             "glib/glib.supp",
                             "gst-python/testsuite/gstpython.supp",
                             "gst-python/testsuite/python.supp",
                             ]:
        suppression = os.path.join(config.SRCDIR, "subprojects", suppression_path)
        if os.path.exists(suppression):
            get_gst_build_valgrind_suppressions.data.append(suppression)

    return get_gst_build_valgrind_suppressions.data


class BackTraceGenerator(Loggable):
    __instance = None
    _command_line_regex = re.compile(r'Command Line: (.*)\n')
    _timestamp_regex = re.compile(r'Timestamp: .*\((\d*)s ago\)')
    _pid_regex = re.compile(r'PID: (\d+) \(.*\)')

    def __init__(self):
        Loggable.__init__(self)

        self.in_flatpak = os.path.exists("/usr/manifest.json")
        if self.in_flatpak:
            coredumpctl = ['flatpak-spawn', '--host', 'coredumpctl']
        else:
            coredumpctl = ['coredumpctl']

        coredumpctl.append('-q')

        try:
            subprocess.check_output(coredumpctl)
            self.coredumpctl = coredumpctl
        except Exception as e:
            self.warning(e)
            self.coredumpctl = None
        self.gdb = shutil.which('gdb')

    @classmethod
    def get_default(cls):
        if not cls.__instance:
            cls.__instance = BackTraceGenerator()

        return cls.__instance

    def get_trace(self, test):
        if not test.process.returncode:
            return self.get_trace_on_running_process(test)

        if self.coredumpctl:
            return self.get_trace_from_systemd(test)

        self.debug("coredumpctl not present, and it is the only"
                   " supported way to get backtraces for now.")
        return None

    def get_trace_on_running_process(self, test):
        if not self.gdb:
            return "Can not generate stack trace as `gdb` is not" \
                "installed."

        gdb = ['gdb', '-ex', 't a a bt', '-batch',
               '-p', str(test.process.pid)]

        try:
            return subprocess.check_output(
                gdb, stderr=subprocess.STDOUT, timeout=30).decode()
        except Exception as e:
            return "Could not run `gdb` on process (pid: %d):\n%s" % (
                test.process.pid, e)

    def get_trace_from_systemd(self, test):
        for ntry in range(10):
            if ntry != 0:
                # Loopping, it means we conceder the logs might not be ready
                # yet.
                time.sleep(1)

            if not self.in_flatpak:
                coredumpctl = self.coredumpctl + ['info', str(test.process.pid)]
            else:
                newer_than = time.strftime("%a %Y-%m-%d %H:%M:%S %Z", time.localtime(test._starting_time))
                coredumpctl = self.coredumpctl + ['info', os.path.basename(test.command[0]),
                                                  '--since', newer_than]

            try:
                info = subprocess.check_output(coredumpctl, stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError:
                # The trace might not be ready yet
                time.sleep(1)
                continue

            info = info.decode()
            try:
                pid = self._pid_regex.findall(info)[0]
            except IndexError:
                self.debug("Backtrace could not be found yet, trying harder.")
                continue

            application = test.process.args[0]
            command_line = BackTraceGenerator._command_line_regex.findall(info)[0]
            if shlex.split(command_line)[0] != application:
                self.debug("PID: %s -- executable %s != test application: %s" % (
                    pid, command_line[0], test.application))
                # The trace might not be ready yet
                continue

            if not BackTraceGenerator._timestamp_regex.findall(info):
                self.debug("Timestamp %s is more than 1min old",
                           re.findall(r'Timestamp: .*', info))
                # The trace might not be ready yet
                continue

            bt_all = None
            if self.gdb:
                try:
                    with tempfile.NamedTemporaryFile() as stderr:
                        coredump = subprocess.check_output(self.coredumpctl + ['dump', pid],
                                                           stderr=stderr)

                    with tempfile.NamedTemporaryFile() as tf:
                        tf.write(coredump)
                        tf.flush()
                        gdb = ['gdb', '-ex', 't a a bt', '-ex', 'quit', application, tf.name]
                        bt_all = subprocess.check_output(
                            gdb, stderr=subprocess.STDOUT).decode()

                        info += "\nThread apply all bt:\n\n%s" % (
                            bt_all.replace('\n', '\n' + 15 * ' '))
                except Exception as e:
                    self.error("Could not get backtrace from gdb: %s" % e)

            return info

        return None


ALL_GITLAB_ISSUES = defaultdict(list)


def check_bugs_resolution(bugs_definitions):
    bugz = {}
    gitlab_issues = defaultdict(list)

    regexes = {}
    mr_id = os.environ.get('CI_MERGE_REQUEST_IID')
    mr_closes_issues = []
    if mr_id:
        gitlab_url = f"{os.environ['CI_API_V4_URL']}/projects/{os.environ['CI_MERGE_REQUEST_PROJECT_ID']}/merge_requests/{mr_id}/closes_issues"
        for issue in json.load(urllib.request.urlopen(gitlab_url)):
            mr_closes_issues.append(issue)

    res = True
    for regex, bugs in bugs_definitions:
        if isinstance(bugs, str):
            bugs = [bugs]

        for bug in bugs:
            url = urllib.parse.urlparse(bug)

            if "gitlab" in url.netloc:
                components = [c for c in url.path.split('/') if c]
                if len(components) not in [4, 5]:
                    printc("\n  + %s \n   --> bug: %s\n   --> Status: Not a proper gitlab report" % (regex, bug),
                           Colors.WARNING)
                    continue
                project_id = components[0] + '%2F' + components[1]
                issue_id = int(components[-1])
                for issue in mr_closes_issues:
                    url = urllib.parse.urlparse(issue['web_url'])
                    closing_issue_project = '%2F'.join([c for c in url.path.split('/') if c][0:2])
                    if project_id == closing_issue_project and issue['iid'] == issue_id:
                        res = False
                        printc("\n  + %s \n   --> %s: '%s'\n   ==> Will be closed by current MR %s\n\n===> Remove blacklisting before merging." % (
                            regex, issue['web_url'], issue['title'], issue['state']), Colors.FAIL)

                gitlab_url = "https://%s/api/v4/projects/%s/issues/%s" % (url.hostname, project_id, issue_id)
                if gitlab_url in ALL_GITLAB_ISSUES:
                    continue
                gitlab_issues[gitlab_url].append(regex)
                ALL_GITLAB_ISSUES[gitlab_url].append(regex)
                continue

            if "bugzilla" not in url.netloc:
                continue

            query = urllib.parse.parse_qs(url.query)
            _id = query.get('id')
            if not _id:
                printc("\n  + '%s' -- Can't check bug '%s'" %
                       (regex, bug), Colors.WARNING)
                continue

            if isinstance(_id, list):
                _id = _id[0]

            regexes[_id] = (regex, bug)
            url_parts = tuple(list(url)[:3] + ['', '', ''])
            ids = bugz.get(url_parts, [])
            ids.append(_id)
            bugz[url_parts] = ids

    for gitlab_url, regexe in gitlab_issues.items():
        try:
            issue = json.load(urllib.request.urlopen(gitlab_url))
        except Exception as e:
            printc("\n  + Could not properly check bugs status for: %s (%s)"
                   % (gitlab_url, e), Colors.FAIL)
            continue

        if issue['state'] in ['closed']:
            printc("\n  + %s \n   --> %s: '%s'\n   ==> Bug CLOSED already (status: %s)" % (
                regexe, issue['web_url'], issue['title'], issue['state']), Colors.FAIL)

            res = False

    for url_parts, ids in bugz.items():
        url_parts = list(url_parts)
        query = {'id': ','.join(ids)}
        query['ctype'] = 'xml'
        url_parts[4] = urllib.parse.urlencode(query)
        try:
            res = urllib.request.urlopen(urllib.parse.urlunparse(url_parts))
        except Exception as e:
            printc("\n  + Could not properly check bugs status for: %s (%s)"
                   % (urllib.parse.urlunparse(url_parts), e), Colors.FAIL)
            continue

        root = ElementTree.fromstring(res.read())
        bugs = root.findall('./bug')

        if len(bugs) != len(ids):
            printc("\n  + Could not properly check bugs status on server %s" %
                   urllib.parse.urlunparse(url_parts), Colors.FAIL)
            continue

        for bugelem in bugs:
            status = bugelem.findtext('./bug_status')
            bugid = bugelem.findtext('./bug_id')
            regex, bug = regexes[bugid]
            desc = bugelem.findtext('./short_desc')

            if not status:
                printc("\n  + %s \n   --> bug: %s\n   --> Status: UNKNOWN" % (regex, bug),
                       Colors.WARNING)
                continue

            if not status.lower() in ['new', 'verified']:
                printc("\n  + %s \n   --> bug: #%s: '%s'\n   ==> Bug CLOSED already (status: %s)" % (
                       regex, bugid, desc, status), Colors.WARNING)

                res = False

            printc("\n  + %s \n   --> bug: #%s: '%s'\n   --> Status: %s" % (
                   regex, bugid, desc, status), Colors.OKGREEN)

    if not res:
        printc("\n==> Some bugs marked as known issues have been (or will be) closed!", Colors.FAIL)

    return res


def kill_subprocess(owner, process, timeout, subprocess_ids=None):
    if process is None:
        return

    stime = time.time()
    res = process.poll()
    waittime = 0.05
    killsig = None
    if not is_windows():
        killsig = signal.SIGINT
    while res is None:
        try:
            owner.debug("Subprocess is still alive, sending KILL signal")
            if is_windows():
                subprocess.call(
                    ['taskkill', '/F', '/T', '/PID', str(process.pid)])
            else:
                if subprocess_ids:
                    for subprocess_id in subprocess_ids:
                        os.kill(subprocess_id, killsig)
                else:
                    process.send_signal(killsig)
            time.sleep(waittime)
            waittime *= 2
        except OSError:
            pass

        if not is_windows() and time.time() - stime > timeout / 4:
            killsig = signal.SIGKILL
        if time.time() - stime > timeout:
            printc("Could not kill %s subprocess after %s second"
                   " Something is really wrong, => EXITING"
                   % (owner, timeout), Colors.FAIL)

            return
        res = process.poll()

    return res


def format_config_template(extra_data, config_text, test_name):
    # Variables available for interpolation inside config blocks.

    extra_vars = extra_data.copy()

    if 'validate-flow-expectations-dir' in extra_vars and \
            'validate-flow-actual-results-dir' in extra_vars:
        expectations_dir = os.path.join(extra_vars['validate-flow-expectations-dir'],
                                        test_name.replace('.', os.sep))
        actual_results_dir = os.path.join(extra_vars['validate-flow-actual-results-dir'],
                                          test_name.replace('.', os.sep))
        extra_vars['validateflow'] = "validateflow, expectations-dir=\"%s\", actual-results-dir=\"%s\"" % (
            expectations_dir, actual_results_dir)

    if 'ssim-results-dir' in extra_vars:
        ssim_results = extra_vars['ssim-results-dir']
        extra_vars['ssim'] = "validatessim, result-output-dir=\"%s\", output-dir=\"%s\"" % (
            os.path.join(ssim_results, test_name.replace('.', os.sep), 'diff-images'),
            os.path.join(ssim_results, test_name.replace('.', os.sep), 'images'),
        )

    return config_text % extra_vars


def get_fakesink_for_media_type(media_type, needs_clock=False):
    extra = ""
    if media_type == "video" and needs_clock:
        extra = 'max-lateness=20000000'

    return f"fake{media_type}sink sync={needs_clock} {extra}"


class InvalidValueError(ValueError):
    """Received value is invalid"""

    def __init__(self, name, value, expect):
        ValueError.__init__(
            self, "Invalid value {!r} for {}. Expect {}.".format(
                value, name, expect))


def wrong_type_for_arg(val, expect_type_name, arg_name):
    """Raise exception in response to a wrong argument type"""
    raise TypeError(
        "Expect a {} type for the '{}' argument. Received a {} type."
        "".format(expect_type_name, arg_name, type(val).__name__))


class DeserializeError(Exception):
    """Receive an incorrectly serialized value"""
    MAX_LEN = 20

    def __init__(self, read, reason):
        if len(read) > self.MAX_LEN:
            read = read[:self.MAX_LEN] + "..."
        Exception.__init__(
            self, "Could not deserialize the string ({}) because it {}."
            "".format(read, reason))


class GstStructure(Loggable):
    """
    This implementation has been copied from OpenTimelineIO.

    Note that the types are to correspond to GStreamer/GES GTypes,
    rather than python types.

    Current supported GTypes:
    GType         Associated    Accepted
                  Python type   aliases
    ======================================
    gint          int           int, i
    glong         int
    gint64        int
    guint         int           uint, u
    gulong        int
    guint64       int
    gfloat        float         float, f
    gdouble       float         double, d
    gboolean      bool          boolean,
                                bool, b
    string        str or None   str, s
    GstFraction   str or        fraction
                  Fraction
    GstStructure  GstStructure  structure
                  schema
    GstCaps       GstCaps
                  schema

    Note that other types can be given: these must be given as strings
    and the user will be responsible for making sure they are already in
    a serialized form.
    """

    INT_TYPES = ("int", "glong", "gint64")
    UINT_TYPES = ("uint", "gulong", "guint64")
    FLOAT_TYPES = ("float", "double")
    BOOLEAN_TYPE = "boolean"
    FRACTION_TYPE = "fraction"
    STRING_TYPE = "string"
    STRUCTURE_TYPE = "structure"
    CAPS_TYPE = "GstCaps"
    KNOWN_TYPES = INT_TYPES + UINT_TYPES + FLOAT_TYPES + (
        BOOLEAN_TYPE, FRACTION_TYPE, STRING_TYPE, STRUCTURE_TYPE,
        CAPS_TYPE)

    TYPE_ALIAS = {
        "i": "int",
        "gint": "int",
        "u": "uint",
        "guint": "uint",
        "f": "float",
        "gfloat": "float",
        "d": "double",
        "gdouble": "double",
        "b": BOOLEAN_TYPE,
        "bool": BOOLEAN_TYPE,
        "gboolean": BOOLEAN_TYPE,
        "GstFraction": FRACTION_TYPE,
        "str": STRING_TYPE,
        "s": STRING_TYPE,
        "GstStructure": STRUCTURE_TYPE
    }

    def __init__(self, name=None, fields=None):
        if name is None:
            name = "Unnamed"
        if fields is None:
            fields = {}
        if type(name) is not str:
            wrong_type_for_arg(name, "str", "name")
        self._check_name(name)
        self.name = name
        try:
            fields = dict(fields)
        except (TypeError, ValueError):
            wrong_type_for_arg(fields, "dict", "fields")
        self.fields = {}
        for key in fields:
            entry = fields[key]
            if type(entry) is not tuple:
                try:
                    entry = tuple(entry)
                except (TypeError, ValueError):
                    raise TypeError(
                        "Expect dict to be filled with tuple-like "
                        "entries")
            if len(entry) != 2:
                raise TypeError(
                    "Expect dict to be filled with 2-entry tuples")
            self.set(key, *entry)

    def __repr__(self):
        return "GstStructure({!r}, {!r})".format(self.name, self.fields)

    UNKNOWN_PREFIX = "[UNKNOWN]"

    @classmethod
    def _make_type_unknown(cls, _type):
        return cls.UNKNOWN_PREFIX + _type
        # note the sqaure brackets make the type break the TYPE_FORMAT

    @classmethod
    def _is_unknown_type(cls, _type):
        return _type[:len(cls.UNKNOWN_PREFIX)] == cls.UNKNOWN_PREFIX

    @classmethod
    def _get_unknown_type(cls, _type):
        return _type[len(cls.UNKNOWN_PREFIX):]

    def _field_to_str(self, key):
        """Return field in a serialized form"""
        _type, value = self.fields[key]
        if type(key) is not str:
            raise TypeError("Found a key that is not a str type")
        if type(_type) is not str:
            raise TypeError(
                "Found a type name that is not a str type")
        self._check_key(key)
        _type = self.TYPE_ALIAS.get(_type, _type)
        if self._is_unknown_type(_type):
            _type = self._get_unknown_type(_type)
            self._check_type(_type)
            self._check_unknown_typed_value(value)
            # already in serialized form
        else:
            self._check_type(_type)
            value = self.serialize_value(_type, value)
        return "{}=({}){}".format(key, _type, value)

    def _fields_to_str(self):
        write = []
        for key in self.fields:
            write.append(", {}".format(self._field_to_str(key)))
        return "".join(write)

    def _name_to_str(self):
        """Return the name in a serialized form"""
        return self._check_name(self.name)

    def __str__(self):
        """Emulates gst_structure_to_string"""
        return "{}{};".format(self._name_to_str(), self._fields_to_str())

    def get_type_name(self, key):
        """Return the field type"""
        _type = self.fields[key][0]
        return _type

    def get_value(self, key):
        """Return the field value"""
        value = self.fields[key][1]
        return value

    def __getitem__(self, key):
        return self.get_value(key)

    def __len__(self):
        return len(self.fields)

    @staticmethod
    def _val_type_err(typ, val, expect):
        raise TypeError(
            "Received value ({!s}) is a {} rather than a {}, even "
            "though the {} type was given".format(
                val, type(val).__name__, expect, typ))

    def set(self, key, _type, value):
        """Set a field to the given typed value"""
        if type(key) is not str:
            wrong_type_for_arg(key, "str", "key")
        if type(_type) is not str:
            wrong_type_for_arg(_type, "str", "_type")
        _type = self.TYPE_ALIAS.get(_type, _type)
        if self.fields.get(key) == (_type, value):
            return
        self._check_key(key)
        type_is_unknown = True
        if self._is_unknown_type(_type):
            # this can happen if the user is setting a GstStructure
            # using a preexisting GstStructure, the type will then
            # be passed and marked as unknown
            _type = self._get_unknown_type(_type)
            self._check_type(_type)
        else:
            self._check_type(_type)
            if _type in self.INT_TYPES:
                type_is_unknown = False
                if not isinstance(value, int):
                    self._val_type_err(_type, value, "int")
            elif _type in self.UINT_TYPES:
                type_is_unknown = False
                if not isinstance(value, int):
                    self._val_type_err(_type, value, "int")
                if value < 0:
                    raise InvalidValueError(
                        "value", value, "a positive integer for {} "
                        "types".format(_type))
            elif _type in self.FLOAT_TYPES:
                type_is_unknown = False
                if type(value) is not float:
                    self._val_type_err(_type, value, "float")
            elif _type == self.BOOLEAN_TYPE:
                type_is_unknown = False
                if type(value) is not bool:
                    self._val_type_err(_type, value, "bool")
            elif _type == self.FRACTION_TYPE:
                type_is_unknown = False
                if type(value) is Fraction:
                    value = value
                elif type(value) is str:
                    try:
                        Fraction(value)
                    except ValueError:
                        raise InvalidValueError(
                            "value", value, "a fraction for the {} "
                            "types".format(_type))
                else:
                    self._val_type_err(_type, value, "Fraction or str")
            elif _type == self.STRING_TYPE:
                type_is_unknown = False
                if value is not None and type(value) is not str:
                    self._val_type_err(_type, value, "str or None")
            elif _type == self.STRUCTURE_TYPE:
                type_is_unknown = False
                if not isinstance(value, GstStructure):
                    self._val_type_err(_type, value, "GstStructure")
            elif _type == self.CAPS_TYPE:
                type_is_unknown = False
                if not isinstance(value, GstCaps):
                    self._val_type_err(_type, value, "GstCaps")
        if type_is_unknown:
            self._check_unknown_typed_value(value)
            warning('GstStructure',
                    "The GstStructure type {} with the value ({}) is "
                    "unknown. The value will be stored and serialized as "
                    "given.".format(_type, value))
            _type = self._make_type_unknown(_type)
        self.fields[key] = (_type, value)

    def get(self, key, default=None):
        """Return the raw value associated with key"""
        if key in self.fields:
            value = self.get_value(key)
            return value
        return default

    def get_typed(self, key, expect_type, default=None):
        """
        Return the raw value associated with key if its type matches.
        Raises a warning if a value exists under key but is of the
        wrong type.
        """
        if type(expect_type) is not str:
            wrong_type_for_arg(expect_type, "str", "expect_type")
        expect_type = self.TYPE_ALIAS.get(expect_type, expect_type)
        if key in self.fields:
            type_name = self.get_type_name(key)
            if expect_type == type_name:
                value = self.get_value(key)
                return value
            warning('GstStructure',
                    "The structure {} contains a value under {}, but is "
                    "a {}, rather than the expected {} type".format(
                        self.name, key, type_name, expect_type))
        return default

    def values(self):
        """Return a list of all values contained in the structure"""
        return [self.get_value(key) for key in self.fields]

    def values_of_type(self, _type):
        """
        Return a list of all values contained of the given type in the
        structure
        """
        if type(_type) is not str:
            wrong_type_for_arg(_type, "str", "_type")
        _type = self.TYPE_ALIAS.get(_type, _type)
        return [self.get_value(key) for key in self.fields
                if self.get_type_name(key) == _type]

    ASCII_SPACES = r"(\\?[ \t\n\r\f\v])*"
    END_FORMAT = r"(?P<end>" + ASCII_SPACES + r")"
    NAME_FORMAT = r"(?P<name>[a-zA-Z][a-zA-Z0-9/_.:-]*)"
    # ^Format requirement for the name of a GstStructure
    SIMPLE_STRING = r"[a-zA-Z0-9_+/:.-]+"
    # see GST_ASCII_CHARS (below)
    KEY_FORMAT = r"(?P<key>" + SIMPLE_STRING + r")"
    # NOTE: GstStructure technically allows more general keys, but
    # these can break the parsing.
    TYPE_FORMAT = r"(?P<type>" + SIMPLE_STRING + r")"
    BASIC_VALUE_FORMAT = \
        r'(?P<value>("(\\.|[^"])*")|(' + SIMPLE_STRING + r'))'
    # consume simple string or a string between quotes. Second will
    # consume anything that is escaped, including a '"'
    # NOTE: \\. is used rather than \\" since:
    #   + '"start\"end;"'  should be captured as '"start\"end"' since
    #     the '"' is escaped.
    #   + '"start\\"end;"' should be captured as '"start\\"' since the
    #     '\' is escaped, not the '"'
    # In the fist case \\. will consume '\"', and in the second it will
    # consumer '\\', as desired. The second would not work with just \\"

    @staticmethod
    def _check_against_regex(check, regex, name):
        if not regex.fullmatch(check):
            raise InvalidValueError(
                name, check, "to match the regular expression {}"
                "".format(regex.pattern))
        return check

    NAME_REGEX = re.compile(NAME_FORMAT)
    KEY_REGEX = re.compile(KEY_FORMAT)
    TYPE_REGEX = re.compile(TYPE_FORMAT)

    @classmethod
    def _check_name(cls, name):
        return cls._check_against_regex(name, cls.NAME_REGEX, "name")

    @classmethod
    def _check_key(cls, key):
        return cls._check_against_regex(key, cls.KEY_REGEX, "key")

    @classmethod
    def _check_type(cls, _type):
        return cls._check_against_regex(_type, cls.TYPE_REGEX, "type")

    @classmethod
    def _check_unknown_typed_value(cls, value):
        if type(value) is not str:
            cls._val_type_err("unknown", value, "string")
        try:
            # see if the value could be successfully parsed in again
            ret_type, ret_val, _ = cls._parse_value(value, False)
        except DeserializeError as err:
            raise InvalidValueError(
                "value", value, "unknown-typed values to be in a "
                "serialized format ({!s})".format(err))
        else:
            if ret_type is not None:
                raise InvalidValueError(
                    "value", value, "unknown-typed values to *not* "
                    "start with a type specification, only the "
                    "serialized value should be given")
            if ret_val != value:
                raise InvalidValueError(
                    "value", value, "unknown-typed values to be the "
                    "same as its parsed value {}".format(ret_val))

    PARSE_NAME_REGEX = re.compile(
        ASCII_SPACES + NAME_FORMAT + END_FORMAT)

    @classmethod
    def _parse_name(cls, read):
        match = cls.PARSE_NAME_REGEX.match(read)
        if match is None:
            raise DeserializeError(
                read, "does not start with a correct name")
        name = match.group("name")
        read = read[match.end("end"):]
        return name, read

    @classmethod
    def _parse_range_list_array(cls, read):
        start = read[0]
        end = {'[': ']', '{': '}', '<': '>'}.get(start)
        read = read[1:]
        values = [start, ' ']
        first = True
        while read and read[0] != end:
            if first:
                first = False
            else:
                if read and read[0] != ',':
                    DeserializeError(
                        read, "does not contain a comma between listed "
                        "items")
                values.append(", ")
                read = read[1:]
            _type, value, read = cls._parse_value(read, False)
            if _type is not None:
                if cls._is_unknown_type(_type):
                    # remove unknown marker for serialization
                    _type = cls._get_unknown_type(_type)
                values.extend(('(', _type, ')'))
            values.append(value)
        if not read:
            raise DeserializeError(
                read, "ended before {} could be found".format(end))
        read = read[1:]  # skip past 'end'
        match = cls.END_REGEX.match(read)  # skip whitespace
        read = read[match.end("end"):]
        # NOTE: we are ignoring the incorrect cases where a range
        # has 0, 1 or 4+ values! This is the users responsiblity.
        values.extend((' ', end))
        return "".join(values), read

    FIELD_START_REGEX = re.compile(
        ASCII_SPACES + KEY_FORMAT + ASCII_SPACES + r"=" + END_FORMAT)
    FIELD_TYPE_REGEX = re.compile(
        ASCII_SPACES + r"(\(" + ASCII_SPACES + TYPE_FORMAT
        + ASCII_SPACES + r"\))?" + END_FORMAT)
    FIELD_VALUE_REGEX = re.compile(
        ASCII_SPACES + BASIC_VALUE_FORMAT + END_FORMAT)
    END_REGEX = re.compile(END_FORMAT)

    @classmethod
    def _parse_value(cls, read, deserialize=True):
        match = cls.FIELD_TYPE_REGEX.match(read)
        # match shouldn't be None since the (TYPE_FORMAT) is optional
        # and the rest is just ASCII_SPACES
        _type = match.group("type")
        if _type is None and deserialize:
            # if deserialize is False, the (type) is optional
            raise DeserializeError(
                read, "does not contain a valid '(type)' format")
        _type = cls.TYPE_ALIAS.get(_type, _type)
        type_is_unknown = True
        read = read[match.end("end"):]
        if read and read[0] in ('[', '{', '<'):
            # range/list/array types
            # this is an unknown type, even though _type itself may
            # be known. e.g. a list on integers will have _type as 'int'
            # but the corresponding value can not be deserialized as an
            # integer
            value, read = cls._parse_range_list_array(read)
            if deserialize:
                # prevent printing on subsequent calls if we find a
                # list within a list, etc.
                warning('GstStructure',
                        "GstStructure received a range/list/array of type "
                        "{}, which can not be deserialized. Storing the "
                        "value as {}.".format(_type, value))
        else:
            match = cls.FIELD_VALUE_REGEX.match(read)
            if match is None:
                raise DeserializeError(
                    read, "does not have a valid value format")
            read = read[match.end("end"):]
            value = match.group("value")
            if deserialize:
                if _type in cls.KNOWN_TYPES:
                    type_is_unknown = False
                    try:
                        value = cls.deserialize_value(_type, value)
                    except DeserializeError as err:
                        raise DeserializeError(
                            read, "contains an invalid typed value "
                            "({!s})".format(err))
                else:
                    warning('GstStructure',
                            "GstStructure found a type {} that is unknown. "
                            "The corresponding value ({}) will not be "
                            "deserialized and will be stored as given."
                            "".format(_type, value))
        if type_is_unknown and _type is not None:
            _type = cls._make_type_unknown(_type)
        return _type, value, read

    @classmethod
    def _parse_field(cls, read):
        match = cls.FIELD_START_REGEX.match(read)
        if match is None:
            raise DeserializeError(
                read, "does not have a valid 'key=...' format")
        key = match.group("key")
        read = read[match.end("end"):]
        _type, value, read = cls._parse_value(read)
        return key, _type, value, read

    @classmethod
    def _parse_fields(cls, read):
        if type(read) is not str:
            wrong_type_for_arg(read, "str", "read")
        fields = {}
        while read and read[0] != ';':
            if read and read[0] != ',':
                DeserializeError(
                    read, "does not separate fields with commas")
            read = read[1:]
            key, _type, value, read = cls._parse_field(read)
            fields[key] = (_type, value)
        if read:
            # read[0] == ';'
            read = read[1:]
        return fields, read

    @classmethod
    def new_from_str(cls, read):
        """
        Returns a new instance of GstStructure, based on the Gst library
        function gst_structure_from_string.
        Strings obtained from the GstStructure str() method can be
        parsed in to recreate the original GstStructure.
        """
        if type(read) is not str:
            wrong_type_for_arg(read, "str", "read")
        name, read = cls._parse_name(read)
        fields = cls._parse_fields(read)[0]
        return GstStructure(name=name, fields=fields)

    @staticmethod
    def _val_read_err(typ, val):
        raise DeserializeError(
            val, "does not translated to the {} type".format(typ))

    @classmethod
    def deserialize_value(cls, _type, value):
        """Return the value as the corresponding type"""
        if type(_type) is not str:
            wrong_type_for_arg(_type, "str", "_type")
        if type(value) is not str:
            wrong_type_for_arg(value, "str", "value")
        _type = cls.TYPE_ALIAS.get(_type, _type)
        if _type in cls.INT_TYPES or _type in cls.UINT_TYPES:
            try:
                value = int(value)
            except ValueError:
                cls._val_read_err(_type, value)
            if _type in cls.UINT_TYPES and value < 0:
                cls._val_read_err(_type, value)
        elif _type in cls.FLOAT_TYPES:
            try:
                value = float(value)
            except ValueError:
                cls._val_read_err(_type, value)
        elif _type == cls.BOOLEAN_TYPE:
            try:
                value = cls.deserialize_boolean(value)
            except DeserializeError:
                cls._val_read_err(_type, value)
        elif _type == cls.FRACTION_TYPE:
            try:
                value = Fraction(value)
            except ValueError:
                cls._val_read_err(_type, value)
        elif _type == cls.STRING_TYPE:
            try:
                value = cls.deserialize_string(value)
            except DeserializeError as err:
                raise DeserializeError(
                    value, "does not translate to a string ({!s})"
                    "".format(err))
        elif _type == cls.STRUCTURE_TYPE:
            try:
                value = cls.deserialize_structure(value)
            except DeserializeError as err:
                raise DeserializeError(
                    value, "does not translate to a GstStructure ({!s})"
                    "".format(err))
        elif _type == cls.CAPS_TYPE:
            try:
                value = cls.deserialize_caps(value)
            except DeserializeError as err:
                raise DeserializeError(
                    value, "does not translate to a GstCaps ({!s})"
                    "".format(err))
        else:
            raise ValueError(
                "The type {} is unknown, so the value ({}) can not "
                "be deserialized.".format(_type, value))
        return value

    @classmethod
    def serialize_value(cls, _type, value):
        """Serialize the typed value as a string"""
        if type(_type) is not str:
            wrong_type_for_arg(_type, "str", "_type")
        _type = cls.TYPE_ALIAS.get(_type, _type)
        if _type in cls.INT_TYPES + cls.UINT_TYPES + cls.FLOAT_TYPES \
                + (cls.FRACTION_TYPE, ):
            return str(value)
        if _type == cls.BOOLEAN_TYPE:
            return cls.serialize_boolean(value)
        if _type == cls.STRING_TYPE:
            return cls.serialize_string(value)
        if _type == cls.STRUCTURE_TYPE:
            return cls.serialize_structure(value)
        if _type == cls.CAPS_TYPE:
            return cls.serialize_caps(value)
        raise ValueError(
            "The type {} is unknown, so the value ({}) can not be "
            "serialized.".format(_type, str(value)))

    # see GST_ASCII_IS_STRING in gst_private.h
    GST_ASCII_CHARS = [
        ord(line) for line in "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        "_-+/:."
    ]
    LEADING_OCTAL_CHARS = [ord(line) for line in "0123"]
    OCTAL_CHARS = [ord(line) for line in "01234567"]

    @classmethod
    def serialize_string(cls, value):
        """
        Emulates gst_value_serialize_string.
        Accepts a bytes, str or None type.
        Returns a str type.
        """
        if value is not None and type(value) is not str:
            wrong_type_for_arg(value, "None or str", "value")
        return cls._wrap_string(value)

    @classmethod
    def _wrap_string(cls, read):
        if read is None:
            return "NULL"
        if read == "NULL":
            return "\"NULL\""
        if type(read) is bytes:
            pass
        elif type(read) is str:
            read = read.encode()
        else:
            wrong_type_for_arg(read, "None, str, or bytes", "read")
        if not read:
            return '""'
        added_wrap = False
        ser_string_list = []
        for byte in read:
            if byte in cls.GST_ASCII_CHARS:
                ser_string_list.append(chr(byte))
            elif byte < 0x20 or byte >= 0x7f:
                ser_string_list.append("\\{:03o}".format(byte))
                added_wrap = True
            else:
                ser_string_list.append("\\" + chr(byte))
                added_wrap = True
        if added_wrap:
            ser_string_list.insert(0, '"')
            ser_string_list.append('"')
        return "".join(ser_string_list)

    @classmethod
    def deserialize_string(cls, read):
        """
        Emulates gst_value_deserialize_string.
        Accepts a str type.
        Returns a str or None type.
        """
        if type(read) is not str:
            wrong_type_for_arg(read, "str", "read")
        if read == "NULL":
            return None
        if not read:
            return ""
        if read[0] != '"' or read[-1] != '"':
            return read
        return cls._unwrap_string(read)

    @classmethod
    def _unwrap_string(cls, read):
        """Emulates gst_string_unwrap"""
        if type(read) is not bytes:
            read_array = read.encode()
        byte_list = []
        bytes_iter = iter(read_array)

        def next_byte():
            try:
                return next(bytes_iter)
            except StopIteration:
                raise DeserializeError(read, "end unexpectedly")

        byte = next_byte()
        if byte != ord('"'):
            raise DeserializeError(
                read, "does not start with '\"', but ends with '\"'")
        while True:
            byte = next_byte()
            if byte in cls.GST_ASCII_CHARS:
                byte_list.append(byte)
            elif byte == ord('"'):
                try:
                    next(bytes_iter)
                except StopIteration:
                    # expect there to be no more bytes
                    break
                raise DeserializeError(
                    read, "contains an un-escaped '\"' before the end")
            elif byte == ord('\\'):
                byte = next_byte()
                if byte in cls.LEADING_OCTAL_CHARS:
                    # could be the start of an octal
                    byte2 = next_byte()
                    byte3 = next_byte()
                    if byte2 in cls.OCTAL_CHARS and byte3 in cls.OCTAL_CHARS:
                        nums = [b - ord('0') for b in (byte, byte2, byte3)]
                        byte = (nums[0] << 6) + (nums[1] << 3) + nums[2]
                        byte_list.append(byte)
                    else:
                        raise DeserializeError(
                            read, "contains the start of an octal "
                            "sequence but not the end")
                else:
                    if byte == 0:
                        raise DeserializeError(
                            read, "contains a null byte after an escape")
                    byte_list.append(byte)
            else:
                raise DeserializeError(
                    read, "contains an unexpected un-escaped character")
        out_str = bytes(bytearray(byte_list))
        try:
            return out_str.decode()
        except (UnicodeError, ValueError):
            raise DeserializeError(
                read, "contains invalid utf-8 byte sequences")

    @staticmethod
    def serialize_boolean(value):
        """
        Emulates gst_value_serialize_boolean.
        Accepts bool type.
        Returns a str type.
        """
        if type(value) is not bool:
            wrong_type_for_arg(value, "bool", "value")
        if value:
            return "true"
        return "false"

    @staticmethod
    def deserialize_boolean(read):
        """
        Emulates gst_value_deserialize_boolean.
        Accepts str type.
        Returns a bool type.
        """
        if type(read) is not str:
            wrong_type_for_arg(read, "str", "read")
        if read.lower() in ("true", "t", "yes", "1"):
            return True
        if read.lower() in ("false", "f", "no", "0"):
            return False
        raise DeserializeError(read, "is an unknown boolean value")

    @classmethod
    def serialize_structure(cls, value):
        """
        Emulates gst_value_serialize_structure.
        Accepts a GstStructure.
        Returns a str type.
        """
        if not isinstance(value, GstStructure):
            wrong_type_for_arg(value, "GstStructure", "value")
        return cls._wrap_string(str(value))

    @classmethod
    def deserialize_structure(cls, read):
        """
        Emulates gst_value_serialize_structure.
        Accepts a str type.
        Returns a GstStructure.
        """
        if type(read) is not str:
            wrong_type_for_arg(read, "str", "read")
        if read[0] == '"':
            # NOTE: since all GstStructure strings end with ';', we
            # don't ever expect the above to *not* be true, but the
            # GStreamer library allows for this case
            try:
                read = cls._unwrap_string(read)
                # NOTE: in the GStreamer library, serialized
                # GstStructure and GstCaps strings are sent to
                # _priv_gst_value_parse_string with unescape set to
                # TRUE. What this essentially does is replace "\x" with
                # just "x". Since caps and structure strings should only
                # contain printable ascii characters before they are
                # passed to _wrap_string, this should be equivalent to
                # calling _unwrap_string. Our method is more clearly a
                # reverse of the serialization method.
            except DeserializeError as err:
                raise DeserializeError(
                    read, "could not be unwrapped as a string ({!s})"
                    "".format(err))
        return GstStructure.new_from_str(read)

    @classmethod
    def serialize_caps(cls, value):
        """
        Emulates gst_value_serialize_caps.
        Accepts a GstCaps.
        Returns a str type.
        """
        if not isinstance(value, GstCaps):
            wrong_type_for_arg(value, "GstCaps", "value")
        return cls._wrap_string(str(value))

    @classmethod
    def deserialize_caps(cls, read):
        """
        Emulates gst_value_serialize_caps.
        Accepts a str type.
        Returns a GstCaps.
        """
        if type(read) is not str:
            wrong_type_for_arg(read, "str", "read")
        if read[0] == '"':
            # can be not true if a caps only contains a single empty
            # structure, or is ALL or NONE
            try:
                read = cls._unwrap_string(read)
            except DeserializeError as err:
                raise DeserializeError(
                    read, "could not be unwrapped as a string ({!s})"
                    "".format(err))
        return GstCaps.new_from_str(read)

    @staticmethod
    def _escape_string(read):
        """
        Emulates some of g_strescape's behaviour in
        ges_marker_list_serialize
        """
        # NOTE: in the original g_strescape, all the special characters
        # '\b', '\f', '\n', '\r', '\t', '\v', '\' and '"' are escaped,
        # and all characters in the range 0x01-0x1F and non-ascii
        # characters are replaced by an octal sequence
        # (similar to _wrap_string).
        # However, a caps string should only contain printable ascii
        # characters, so it should be sufficient to simply escape '\'
        # and '"'.
        escaped = ['"']
        for character in read:
            if character in ('"', '\\'):
                escaped.append('\\')
            escaped.append(character)
        escaped.append('"')
        return "".join(escaped)

    @staticmethod
    def _unescape_string(read):
        """
        Emulates behaviour of _priv_gst_value_parse_string with
        unescape set to TRUE. This should undo _escape_string
        """
        if read[0] != '"':
            return read
        character_iter = iter(read)

        def next_char():
            try:
                return next(character_iter)
            except StopIteration:
                raise DeserializeError(read, "ends unexpectedly")

        next_char()  # skip '"'
        unescaped = []
        while True:
            character = next_char()
            if character == '"':
                break
            if character == '\\':
                unescaped.append(next_char())
            else:
                unescaped.append(character)
        return "".join(unescaped)


class GstCapsFeatures():
    """
    Mimicking a GstCapsFeatures.
    """

    def __init__(self, *features):
        """
        Initialize the GstCapsFeatures.

        'features' should be a series of feature names as strings.
        """
        self.is_any = False
        self.features = []
        for feature in features:
            if type(feature) is not str:
                wrong_type_for_arg(feature, "strs", "features")
            self._check_feature(feature)
            self.features.append(feature)
            # NOTE: if 'features' is a str, rather than a list of strs
            # then this will iterate through all of its characters! But,
            # a single character can not match the feature regular
            # expression.

    def __getitem__(self, index):
        return self.features[index]

    def __len__(self):
        return len(self.features)

    @classmethod
    def new_any(cls):
        features = cls()
        features.is_any = True
        return features

    # Based on gst_caps_feature_name_is_valid
    FEATURE_FORMAT = r"(?P<feature>[a-zA-Z]*:[a-zA-Z][a-zA-Z0-9]*)"
    FEATURE_REGEX = re.compile(FEATURE_FORMAT)

    @classmethod
    def _check_feature(cls, feature):
        if not cls.FEATURE_REGEX.fullmatch(feature):
            raise InvalidValueError(
                "feature", feature, "to match the regular expression "
                "{}".format(cls.FEATURE_REGEX.pattern))

    PARSE_FEATURE_REGEX = re.compile(
        r" *" + FEATURE_FORMAT + "(?P<end>)")

    @classmethod
    def new_from_str(cls, read):
        """
        Returns a new instance of GstCapsFeatures, based on the Gst
        library function gst_caps_features_from_string.
        Strings obtained from the GstCapsFeatures str() method can be
        parsed in to recreate the original GstCapsFeatures.
        """
        if type(read) is not str:
            wrong_type_for_arg(read, "str", "read")
        if read == "ANY":
            return cls.new_any()
        first = True
        features = []
        while read:
            if first:
                first = False
            else:
                if read[0] != ',':
                    DeserializeError(
                        read, "does not separate features with commas")
                read = read[1:]
            match = cls.PARSE_FEATURE_REGEX.match(read)
            if match is None:
                raise DeserializeError(
                    read, "does not match the regular expression {}"
                    "".format(cls.PARSE_FEATURE_REGEX.pattern))
            features.append(match.group("feature"))
            read = read[match.end("end"):]
        return cls(*features)

    def __repr__(self):
        if self.is_any:
            return "GstCapsFeatures.new_any()"
        write = ["GstCapsFeatures("]
        first = True
        for feature in self.features:
            if first:
                first = False
            else:
                write.append(", ")
            write.append(repr(feature))
        write.append(")")
        return "".join(write)

    def __str__(self):
        """Emulate gst_caps_features_to_string"""
        if not self.features and self.is_any:
            return "ANY"
        write = []
        first = True
        for feature in self.features:
            if type(feature) is not str:
                raise TypeError(
                    "Found a feature that is not a str type")
            if first:
                first = False
            else:
                write.append(", ")
            write.append(feature)
        return "".join(write)


class GstCaps:
    GST_CAPS_FLAG_ANY = 1 << 4
    # from GST_MINI_OBJECT_FLAG_LAST

    def __init__(self, *structs):
        """
        Initialize the GstCaps.

        'structs' should be a series of GstStructures, and
        GstCapsFeatures pairs:
            struct0, features0, struct1, features1, ...
        None may be given in place of a GstCapsFeatures, in which case
        an empty features is assigned to the structure.

        Note, this instance will need to take ownership of any given
        GstStructure or GstCapsFeatures.
        """
        if len(structs) % 2:
            raise InvalidValueError(
                "*structs", structs, "an even number of arguments")
        self.flags = 0
        self.structs = []
        struct = None
        for index, arg in enumerate(structs):
            if index % 2 == 0:
                struct = arg
            else:
                self.append(struct, arg)

    def get_structure(self, index):
        """Return the GstStructure at the given index"""
        return self.structs[index][0]

    def get_features(self, index):
        """Return the GstStructure at the given index"""
        return self.structs[index][1]

    def __getitem__(self, index):
        return self.get_structure(index)

    def __len__(self):
        return len(self.structs)

    def __iter__(self):
        for s in self.structs:
            yield s

    @classmethod
    def new_any(cls):
        caps = cls()
        caps.flags = cls.GST_CAPS_FLAG_ANY
        return caps

    def is_any(self):
        return self.flags & self.GST_CAPS_FLAG_ANY != 0

    FEATURES_FORMAT = r"\((?P<features>[^)]*)\)"
    NAME_FEATURES_REGEX = re.compile(
        GstStructure.ASCII_SPACES + GstStructure.NAME_FORMAT
        + r"(" + FEATURES_FORMAT + r")?" + GstStructure.END_FORMAT)

    @classmethod
    def new_from_str(cls, read):
        """
        Returns a new instance of GstCaps, based on the Gst library
        function gst_caps_from_string.
        Strings obtained from the GstCaps str() method can be parsed in
        to recreate the original GstCaps.
        """
        if type(read) is not str:
            wrong_type_for_arg(read, "str", "read")
        if read == "ANY":
            return cls.new_any()
        if read in ("EMPTY", "NONE"):
            return cls()
        structs = []
        # restriction-caps is otherwise serialized in the format:
        #   "struct-name-nums(feature), "
        #   "field1=(type1)val1, field2=(type2)val2; "
        #   "struct-name-alphas(feature), "
        #   "fieldA=(typeA)valA, fieldB=(typeB)valB"
        # Note the lack of ';' for the last structure, and the
        # '(feature)' is optional.
        #
        # NOTE: gst_caps_from_string also accepts:
        #   "struct-name(feature"
        # without the final ')', but this must be the end of the string,
        # but we will require that this final ')' is still given
        while read:
            match = cls.NAME_FEATURES_REGEX.match(read)
            if match is None:
                raise DeserializeError(
                    read, "does not match the regular expression {}"
                    "".format(cls.NAME_FEATURE_REGEX.pattern))
            read = read[match.end("end"):]
            name = match.group("name")
            features = match.group("features")
            # NOTE: features may be None since the features part of the
            # regular expression is optional
            if features is None:
                features = GstCapsFeatures()
            else:
                features = GstCapsFeatures.new_from_str(features)
            fields, read = GstStructure._parse_fields(read)
            structs.append(GstStructure(name, fields))
            structs.append(features)
        return cls(*structs)

    def __repr__(self):
        if self.is_any():
            return "GstCaps.new_any()"
        write = ["GstCaps("]
        first = True
        for struct in self.structs:
            if first:
                first = False
            else:
                write.append(", ")
            write.append(repr(struct[0]))
            write.append(", ")
            write.append(repr(struct[1]))
        write.append(")")
        return "".join(write)

    def __str__(self):
        """Emulate gst_caps_to_string"""
        if self.is_any():
            return "ANY"
        if not self.structs:
            return "EMPTY"
        first = True
        write = []
        for struct, features in self.structs:
            if first:
                first = False
            else:
                write.append("; ")
            write.append(struct._name_to_str())
            if features.is_any or features.features:
                # NOTE: is gst_caps_to_string, the feature will not
                # be written if it only contains the
                # GST_FEATURE_MEMORY_SYSTEM_MEMORY feature, since this
                # considered equal to being an empty features.
                # We do not seem to require this behaviour
                write.append("({!s})".format(features))
            write.append(struct._fields_to_str())
        return "".join(write)

    def append(self, structure, features=None):
        """Append a structure with the given features"""
        if not isinstance(structure, GstStructure):
            wrong_type_for_arg(structure, "GstStructure", "structure")
        if features is None:
            features = GstCapsFeatures()
        if not isinstance(features, GstCapsFeatures):
            wrong_type_for_arg(
                features, "GstCapsFeatures or None", "features")
        self.structs.append((structure, features))
