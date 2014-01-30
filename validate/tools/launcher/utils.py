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
""" Some utilies. """

import os
import urllib
import loggable
import urlparse
import subprocess


GST_SECOND = 1000000000
DEFAULT_TIMEOUT = 10
DEFAULT_GST_QA_ASSETS = os.path.join(os.path.expanduser('~'), "Videos",
                          "gst-qa-assets")
DISCOVERER_COMMAND = "gst-discoverer-1.0"
DURATION_TOLERANCE = GST_SECOND / 2


class Result(object):
    NOT_RUN = "Not run"
    FAILED = "Failed"
    TIMEOUT = "Timeout"
    PASSED = "Passed"


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


def which(name):
    result = []
    exts = filter(None, os.environ.get('PATHEXT', '').split(os.pathsep))
    path = os.environ.get('PATH', None)
    if path is None:
        return []
    for p in os.environ.get('PATH', '').split(os.pathsep):
        p = os.path.join(p, name)
        if os.access(p, os.X_OK):
            result.append(p)
        for e in exts:
            pext = p + e
            if os.access(pext, os.X_OK):
                result.append(pext)
    return result


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

    print color + str(message) + Colors.ENDC


def launch_command(command, color=None):
    printc(command, Colors.OKGREEN, True)
    os.system(command)


def path2url(path):
    return urlparse.urljoin('file:', urllib.pathname2url(path))


def url2path(url):
    return urlparse.urlparse(url).path


def isuri(string):
    url = urlparse.urlparse(string)
    if url.scheme != "" and  url.scheme != "":
        return True

    return False


##############################
#    Encoding related utils  #
##############################
class MediaFormatCombination(object):

    def __str__(self):
        return "%s and %s in %s" % (self.acodec, self.vcodec, self.container)

    def __init__(self, container, acodec, vcodec):
        self.container = container
        self.acodec = acodec
        self.vcodec = vcodec


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


def get_profile_full(muxer, venc, aenc, video_restriction=None,
                     audio_restriction=None,
                     audio_presence=0, video_presence=0):
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


def get_profile(combination):
    return get_profile_full(FORMATS[combination.container],
                            FORMATS[combination.vcodec],
                            FORMATS[combination.acodec],
                            video_restriction="video/x-raw,format=I420")

##################################################
#  Some utilities to parse gst-validate output   #
##################################################
def parse_gsttimeargs(time):
    stime = time.split(":")
    sns = stime[2].split(".")
    stime[2] = sns[0]
    stime.append(sns[1])
    return long((int(stime[0]) * 3600 + int(stime[1]) * 60 + int(stime[2]) * 60) * GST_SECOND +  int(stime[3]))

def get_duration(media_file):

    duration = 0
    try:
        res = subprocess.check_output([DISCOVERER_COMMAND, media_file])
    except subprocess.CalledProcessError:
        # gst-media-check returns !0 if seeking is not possible, we do not care in that case.
        pass

    for l in res.split('\n'):
        if "Duration: " in l:
            duration = parse_gsttimeargs(l.replace("Duration: ", ""))
            break

    return duration


def compare_rendered_with_original(orig_duration, dest_file, tolerance=DURATION_TOLERANCE):
        duration = get_duration(dest_file)

        if orig_duration - tolerance >= duration >= orig_duration + tolerance:
            return (Result.FAILED, "Duration of encoded file is "
                    " wrong (%s instead of %s)" %
                    (orig_duration / GST_SECOND,
                    duration / GST_SECOND),
                    "wrong-duration")
        else:
            return (Result.PASSED, "")
