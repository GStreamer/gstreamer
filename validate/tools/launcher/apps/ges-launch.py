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
import urlparse
from urllib import unquote
from gi.repository import GES, Gst, GLib
from baseclasses import GstValidateTest, TestsManager
from utils import MediaFormatCombination, get_profile, Result, get_current_position, \
    get_current_size, DEFAULT_GST_QA_ASSETS, which, \
    compare_rendered_with_original, get_duration

DURATION_TOLERANCE = Gst.SECOND / 2
DEFAULT_GES_LAUNCH = "ges-launch-1.0"


COMBINATIONS = [
    MediaFormatCombination("ogg", "vorbis", "theora"),
    MediaFormatCombination("webm", "vorbis", "vp8"),
    MediaFormatCombination("mp4", "mp3", "h264"),
    MediaFormatCombination("mkv", "vorbis", "h264")]


SCENARIOS = ["none", "seek_forward", "seek_backward", "scrub_forward_seeking"]


def quote_uri(uri):
    """
    Encode a URI/path according to RFC 2396, without touching the file:/// part.
    """
    # Split off the "file:///" part, if present.
    parts = urlparse.urlsplit(uri, allow_fragments=False)
    # Make absolutely sure the string is unquoted before quoting again!
    raw_path = unquote(parts.path)
    # For computing thumbnail md5 hashes in the media library, we must adhere to
    # RFC 2396. It is quite tricky to handle all corner cases, leave it to Gst:
    return Gst.filename_to_uri(raw_path)


class GESTest(GstValidateTest):
    def __init__(self, classname, options, reporter, project_uri, scenario=None,
                 combination=None):
        super(GESTest, self).__init__(DEFAULT_GES_LAUNCH, classname, options, reporter,
                                      scenario=scenario)
        self.project_uri = project_uri
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
        GstValidateTest.build_arguments(self)

        if self.options.mute:
            self.add_arguments(" --mute")

        self.set_sample_paths()
        self.add_arguments("-l", self.project_uri)


class GESPlaybackTest(GESTest):
    def __init__(self, classname, options, reporter, project_uri, scenario):
        super(GESPlaybackTest, self).__init__(classname, options, reporter,
                                      project_uri, scenario=scenario)

    def get_current_value(self):
        return get_current_position(self)


class GESRenderTest(GESTest):
    def __init__(self, classname, options, reporter, project_uri, combination):
        super(GESRenderTest, self).__init__(classname, options, reporter,
                                      project_uri)
        self.combination = combination

    def build_arguments(self):
        GESTest.build_arguments(self)
        self._set_rendering_info()

    def _set_rendering_info(self):
        self.dest_file = os.path.join(self.options.dest,
                                 os.path.basename(self.project_uri) +
                                 '-' + self.combination.acodec +
                                 self.combination.vcodec + '.' +
                                 self.combination.container)
        if not Gst.uri_is_valid(self.dest_file):
            self.dest_file = GLib.filename_to_uri(self.dest_file, None)

        profile = get_profile(self.combination)
        self.add_arguments("-f", profile, "-o", self.dest_file)

    def check_results(self):
        if self.process.returncode == 0:
            res, msg = compare_rendered_with_original(self.duration, self.dest_file)
            self.set_result(res, msg)
        else:
            if self.result == Result.TIMEOUT:
                missing_eos = False
                try:
                    if get_duration(self.dest_file) == self.duration:
                        missing_eos = True
                except Exception as e:
                    pass

                if missing_eos is True:
                    self.set_result(Result.TIMEOUT, "The rendered file add right duration, MISSING EOS?\n",
                                    "failure", e)
            else:
                GstValidateTest.check_results(self)

    def get_current_value(self):
        return get_current_size(self)


class GESTestsManager(TestsManager):
    name = "ges"

    def __init__(self):
        super(GESTestsManager, self).__init__()
        Gst.init(None)
        GES.init()


    def init(self):
        if which(DEFAULT_GES_LAUNCH):
            return True

        return False

    def add_options(self, group):
        group.add_option("-P", "--projects-paths", dest="projects_paths",
                         default=os.path.join(DEFAULT_GST_QA_ASSETS, "ges-projects"),
                         help="Paths in which to look for moved medias")
        group.add_option("-r", "--recurse-paths", dest="recurse_paths",
                         default=False, action="store_true",
                         help="Whether to recurse into paths to find medias")

    def set_settings(self, options, args, reporter):
        TestsManager.set_settings(self, options, args, reporter)

        try:
            os.makedirs(GLib.filename_from_uri(options.dest)[0])
            print "Created directory: %s" % options.dest
        except OSError:
            pass

    def list_tests(self):
        projects = list()
        if not self.args:
            path = self.options.projects_paths
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
                self.tests.append(GESPlaybackTest(classname,
                                                  self.options,
                                                  self.reporter,
                                                  proj,
                                                  scenario=scenario)
                                  )

            # And now rendering casses
            for comb in COMBINATIONS:
                classname = "ges.render.%s.%s" % (str(comb).replace(' ', '_'),
                                                  os.path.splitext(os.path.basename(proj))[0])
                self.tests.append(GESRenderTest(classname, self.options,
                                                self.reporter, proj,
                                                combination=comb)
                                  )
