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
""" Some utilies. """

import config
import os
import re
import sys
import urllib
import urlparse
import subprocess

from operator import itemgetter


GST_SECOND = long(1000000000)
DEFAULT_TIMEOUT = 30
DEFAULT_MAIN_DIR = os.path.join(os.path.expanduser("~"), "gst-validate")
DEFAULT_GST_QA_ASSETS = os.path.join(DEFAULT_MAIN_DIR, "gst-integration-testsuites")
DISCOVERER_COMMAND = "gst-discoverer-1.0"
# Use to set the duration from which a test is considered as being 'long'
LONG_TEST = 40


class Result(object):
    NOT_RUN = "Not run"
    FAILED = "Failed"
    TIMEOUT = "Timeout"
    PASSED = "Passed"
    KNOWN_ERROR = "Known error"


class Protocols(object):
    HTTP = "http"
    FILE = "file"
    HLS = "hls"
    DASH = "dash"

    @staticmethod
    def needs_clock_sync(protocol):
        if protocol in [Protocols.HLS, Protocols.DASH]:
            return True

        return False


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


def mkdir(directory):
    try:
        os.makedirs(directory)
    except os.error:
        pass


def which(name, extra_path=None):
    exts = filter(None, os.environ.get('PATHEXT', '').split(os.pathsep))
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


def printc(message, color="", title=False):
    if title:
        length = 0
        for l in message.split("\n"):
            if len(l) > length:
                length = len(l)
        if length == 0:
            length = len(message)
        message = length * '=' + "\n" + str(message) + "\n" + length * '='

    if hasattr(message, "result") and color == '':
        color = get_color_for_result(message.result)

    sys.stdout.write(color + str(message) + Colors.ENDC + "\n")
    sys.stdout.flush()


def launch_command(command, color=None, fails=False):
    printc(command, Colors.OKGREEN, True)
    res = os.system(command)
    if res != 0 and fails is True:
        raise subprocess.CalledProcessError(res, "%s failed" % command)


def path2url(path):
    return urlparse.urljoin('file:', urllib.pathname2url(path))


def url2path(url):
    path = urlparse.urlparse(url).path
    if "win32" in sys.platform:
        if path[0] == '/':
            return path[1:]  # We need to remove the first '/' on windows
    path = urllib.unquote(path)
    return path


def isuri(string):
    url = urlparse.urlparse(string)
    if url.scheme != "" and url.scheme != "":
        return True

    return False


def touch(fname, times=None):
    with open(fname, 'a'):
        os.utime(fname, times)


def get_subclasses(klass, env):
    subclasses = []
    for symb in env.iteritems():
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
    root_dir = os.path.abspath(os.path.dirname(os.path.join(os.path.dirname(os.path.abspath(__file__)))))
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
    return long((int(stime[0]) * 3600 + int(stime[1]) * 60 + int(stime[2])) * GST_SECOND + int(stime[3]))

timeregex = re.compile(r'(?P<_0>.+):(?P<_1>.+):(?P<_2>.+)\.(?P<_3>.+)')


def parse_gsttimeargs(time):
    stime = map(itemgetter(1), sorted(
        timeregex.match(time).groupdict().items()))
    return long((int(stime[0]) * 3600 + int(stime[1]) * 60 + int(stime[2])) * GST_SECOND + int(stime[3]))


def get_duration(media_file):

    duration = 0
    res = ''
    try:
        res = subprocess.check_output([DISCOVERER_COMMAND, media_file])
    except subprocess.CalledProcessError:
        # gst-media-check returns !0 if seeking is not possible, we do not care
        # in that case.
        pass

    for l in res.split('\n'):
        if "Duration: " in l:
            duration = parse_gsttimeargs(l.replace("Duration: ", ""))
            break

    return duration


def get_scenarios():
    GST_VALIDATE_COMMAND = "gst-validate-1.0"
    os.system("%s --scenarios-defs-output-file %s" % (GST_VALIDATE_COMMAND,
                                                      ))
