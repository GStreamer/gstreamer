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
import urlparse


GST_SECOND = 1000000000


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


def desactivate_colors(self):
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


def printc (message, color="", title=False):
    if title:
        message = len(message) * '=' + message + len(message) * '='
    if hasattr(message, "result") and color == '':
        if message.result == Result.FAILED:
            color = Colors.FAIL
        elif message.result == Result.PASSED:
            color = Colors.OKGREEN
        else:
            color = Colors.OKBLUE

    print color + str(message) + Colors.ENDC


def launch_command(command, color=None):
    printc(command, Colors.OKGREEN, True)
    os.system(command)

def path2url(path):
    return urlparse.urljoin('file:', urllib.pathname2url(path))


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
           "webm": "video/x-matroska"}

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


def _parse_position(p):
    def parse_gsttimeargs(time):
        return int(time.split(":")[0]) * 3600 + int(time.split(":")[1]) * 60 + int(time.split(":")[2].split(".")[0]) * 60

    start_stop = p.replace("<Position: ", '').replace("/>", '').split(" / ")

    return parse_gsttimeargs(start_stop[0]), parse_gsttimeargs(start_stop[1])


def _get_position(test):
    position = duration = 0

    test.reporter.out.seek(0)
    m = None
    for l in test.reporter.out.readlines():
        if "<Position:" in l:
            m = l

    if m is None:
        return position, duration

    for j in m.split("\r"):
        if j.startswith("<Position:") and j.endswith("/>"):
            position, duration = _parse_position(j)

    return position, duration


def get_current_position(test, max_passed_stop=0.5):
    position, duration = _get_position(test)

    if position > duration + max_passed_stop:
        test.set_result(Result.FAILED,
                        "The position is reported as > than the"
                        " duration (position: %d > duration: %d)"
                        % (position, duration))
        return Result.FAILED

    return position


def get_current_size(test):
    position = get_current_position(test)

    if position is Result.FAILED:
        return position

    return os.stat(urlparse.urlparse(test.dest_file).path).st_size
