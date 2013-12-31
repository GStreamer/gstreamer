#!/usr//bin/python
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

import os
import time
from urllib import unquote
from urlparse import urlsplit
from utils import launch_command
from gi.repository import GES, Gst, GLib

DURATION_TOLERANCE = Gst.SECOND / 2
DEFAULT_GES_LAUNCH = "ges-launch-1.0"


class Combination(object):

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

COMBINATIONS = [
    Combination("ogg", "vorbis", "theora"),
    Combination("webm", "vorbis", "vp8"),
    Combination("mp4", "mp3", "h264"),
    Combination("mkv", "vorbis", "h264")]


SCENARIOS = ["none", "seek_forward", "seek_backward", "scrub_forward_seeking"]


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


def quote_uri(uri):
    """
    Encode a URI/path according to RFC 2396, without touching the file:/// part.
    """
    # Split off the "file:///" part, if present.
    parts = urlsplit(uri, allow_fragments=False)
    # Make absolutely sure the string is unquoted before quoting again!
    raw_path = unquote(parts.path)
    # For computing thumbnail md5 hashes in the media library, we must adhere to
    # RFC 2396. It is quite tricky to handle all corner cases, leave it to Gst:
    return Gst.filename_to_uri(raw_path)


class GESTest(Test):
    def __init__(self, classname, options, reporter, project_uri, scenario,
                 combination=None):
        super(GESTest, self).__init__(DEFAULT_GES_LAUNCH, classname, options, reporter)
        self.scenario = scenario
        self.project_uri = project_uri
        self.combination = combination
        proj = GES.Project.new(project_uri)
        tl = proj.extract()
        if tl is None:
            self.duration = None
        else:
            self.duration = tl.get_meta("duration")
            if self.duration is not None:
                self.duration = self.duration / Gst.SECOND
            else:
                self.duration = 2 * 60

    def set_rendering_info(self):
        self.dest_file = os.path.join(self.options.dest,
                                 os.path.basename(self.project_uri) +
                                 '-' + self.combination.acodec +
                                 self.combination.vcodec + '.' +
                                 self.combination.container)
        if not Gst.uri_is_valid(self.dest_file):
            self.dest_file = GLib.filename_to_uri(self.dest_file, None)

        profile = get_profile(self.combination)
        self.add_arguments("-f", profile, "-o", self.dest_file)

    def set_sample_paths(self):
        if not self.options.paths:
            if not self.options.recurse_paths:
                return
            paths = [os.path.dirname(Gst.uri_get_location(self.project_uri))]
        else:
            paths = self.options.paths

        for path in paths:
            if self.options.recurse_paths:
                self.add_arguments("--sample-paths", quote_uri(path))
                for root, dirs, files in os.walk(path):
                    for directory in dirs:
                        self.add_arguments("--sample-paths",
                                           quote_uri(os.path.join(path,
                                                                  root,
                                                                  directory)
                                                     )
                                          )
            else:
                self.add_arguments("--sample-paths", "file://" + path)

    def build_arguments(self):
        print "\OOO %s" % self.combination
        if self.scenario is not None:
            self.add_arguments("--set-scenario", self.scenario)
        if self.combination is not None:
            self.set_rendering_info()

        if self.options.mute:
            self.add_arguments(" --mute")

        self.set_sample_paths()
        self.add_arguments("-l", self.project_uri)

    def check_results(self):
        if self.process.returncode == 0:
            if self.combination:
                try:
                    asset = GES.UriClipAsset.request_sync(self.dest_file)
                    if self.duration - DURATION_TOLERANCE <= asset.get_duration() \
                            <= self.duration + DURATION_TOLERANCE:
                        self.set_result(Result.FAILURE, "Duration of encoded file is "
                                        " wrong (%s instead of %s)" %
                                        (Gst.TIME_ARGS(self.duration),
                                         Gst.TIME_ARGS(asset.get_duration())),
                                        "wrong-duration")
                    else:
                        self.set_result(Result.PASSED)
                except GLib.Error as e:
                    self.set_result(Result.FAILURE, "Wrong rendered file", "failure", e)

            else:
                self.set_result(Result.PASSED)
        else:
            if self.combination and self.result == Result.TIMEOUT:
                missing_eos = False
                try:
                    asset = GES.UriClipAsset.request_sync(self.dest_file)
                    if asset.get_duration() == self.duration:
                        missing_eos = True
                except Exception as e:
                    pass

                if missing_eos is True:
                    self.set_result(Result.TIMEOUT, "The rendered file add right duration, MISSING EOS?\n",
                                     "failure", e)
            else:
                if self.result == Result.TIMEOUT:
                    self.set_result(Result.TIMEOUT, "Application timed out", "timeout")
                else:
                    if self.process.returncode == 139:
                        self.get_backtrace("SEGFAULT")
                        self.set_result("Application segfaulted")
                    else:
                        self.set_result(Result.FAILED,
                            "Application returned %d (issues: %s)" % (
                            self.process.returncode,
                            self.get_validate_criticals_errors()),
                            "error")


    def wait_process(self):
        last_val = 0
        last_change_ts = time.time()
        while True:
            self.process.poll()
            if self.process.returncode is not None:
                self.check_results()
                break

            # Dirty way to avoid eating to much CPU... good enough for us anyway.
            time.sleep(1)

            if self.combination:
                val = os.stat(GLib.filename_from_uri(self.dest_file)[0]).st_size
            else:
                val = self.get_last_position()

            if val == last_val:
                if time.time() - last_change_ts > 10:
                    self.result = Result.TIMEOUT
            else:
                last_change_ts = time.time()
                last_val = val


    def get_last_position(self):
        self.reporter.out.seek(0)
        m = None
        for l in self.reporter.out.readlines():
            if "<Position:" in l:
                m = l

        if m is None:
            return ""

        pos = ""
        for j in m.split("\r"):
            if j.startswith("<Position:") and j.endswith("/>"):
                pos = j

        return pos


class GESTestsManager(TestsManager):
    def __init__(self):
        super(GESTestsManager, self).__init__()
        Gst.init(None)
        GES.init()

        default_opath = GLib.get_user_special_dir(
            GLib.UserDirectory.DIRECTORY_VIDEOS)
        if default_opath:
            self.default_path = os.path.join(default_opath, "ges-projects")
        else:
            self.default_path = os.path.join(os.path.expanduser('~'), "Video",
                                    "ges-projects")

    def add_options(self, parser):
        parser.add_option("-o", "--output-path", dest="dest",
                          default=os.path.join(self.default_path, "rendered"),
                          help="Set the path to which projects should be"
                          " renderd")
        parser.add_option("-P", "--sample-path", dest="paths",
                          default=[],
                          help="Paths in which to look for moved assets")
        parser.add_option("-r", "--recurse-paths", dest="recurse_paths",
                          default=False, action="store_true",
                          help="Whether to recurse into paths to find assets")
        parser.add_option("-m", "--mute", dest="mute",
                          action="store_true", default=False,
                          help="Mute playback output, which mean that we use "
                               "a fakesink")


    def set_settings(self, options, args, reporter):
        TestsManager.set_settings(self, options, args, reporter)
        if not args and not os.path.exists(self.default_path):
            launch_command("git clone %s" % DEFAULT_ASSET_REPO,
                           "Getting assets")

        if not Gst.uri_is_valid(options.dest):
            options.dest = GLib.filename_to_uri(options.dest, None)

        try:
            os.makedirs(GLib.filename_from_uri(options.dest)[0])
            print "Created directory: %s" % options.dest
        except OSError:
            pass

    def list_tests(self):
        projects = list()
        if not self.args:
            self.options.paths = [os.path.join(self.default_path, "assets")]
            path = os.path.join(self.default_path, "projects")
            for root, dirs, files in os.walk(path):
                for f in files:
                    if not f.endswith(".xges"):
                        continue

                    projects.append(GLib.filename_to_uri(os.path.join(path,
                                                                      root,
                                                                      f),
                                    None))
        else:
            for proj in self.args:
                if Gst.uri_is_valid(proj):
                    projects.append(proj)
                else:
                    projects.append(GLib.filename_to_uri(proj, None))

        for proj in projects:
            # First playback casses
            for scenario in SCENARIOS:
                classname = "ges.playback.%s.%s" % (scenario, os.path.basename(proj).replace(".xges", ""))
                self.tests.append(GESTest(classname,
                                          self.options,
                                          self.reporter,
                                          proj,
                                          scenario)
                                  )

            # And now rendering casses
            for comb in COMBINATIONS:
                classname = "ges.render.%s.%s" % (str(comb).replace(' ', '_'),
                                              os.path.basename(proj).replace(".xges", ""))
                self.tests.append(GESTest(classname,
                                          self.options,
                                          self.reporter,
                                          proj,
                                          None,
                                          comb)
                                  )
