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
import os
import urlparse
import subprocess
import ConfigParser
from loggable import Loggable

from baseclasses import GstValidateTest, TestsManager, Test
from utils import MediaFormatCombination, get_profile,\
    path2url, get_current_position, get_current_size, \
    DEFAULT_TIMEOUT, which, GST_SECOND, Result, \
    compare_rendered_with_original


DEFAULT_GST_VALIDATE = "gst-validate-1.0"
DEFAULT_GST_VALIDATE_TRANSCODING = "gst-validate-transcoding-1.0"
DISCOVERER_COMMAND = ["gst-validate-media-check-1.0"]
MEDIA_INFO_EXT = "media_info"
STREAM_INFO = "stream_info"

SPECIAL_PROTOCOLS = [("application/x-hls", "hls")]

PLAYBACK_TESTS = ["playbin uri=__uri__ audio_sink=autoaudiosink video_sink=autovideosink"]
COMBINATIONS = [
    MediaFormatCombination("ogg", "vorbis", "theora"),
    MediaFormatCombination("webm", "vorbis", "vp8"),
    MediaFormatCombination("mp4", "mp3", "h264"),
    MediaFormatCombination("mkv", "vorbis", "h264")]


class NamedDic(object):

    def __init__(self, props):
        for name, value in props.iteritems():
            setattr(self, name, value)


class GstValidateLaunchTest(GstValidateTest):
    def __init__(self, classname, options, reporter, pipeline_desc,
                 timeout=DEFAULT_TIMEOUT, scenario=None, file_infos=None):
        super(GstValidateLaunchTest, self).__init__(DEFAULT_GST_VALIDATE, classname,
                                              options, reporter,
                                              scenario=scenario,)
        self.pipeline_desc = pipeline_desc
        self.file_infos = file_infos

    def build_arguments(self):
        GstValidateTest.build_arguments(self)
        self.add_arguments(self.pipeline_desc)

    def get_current_value(self):
        return get_current_position(self)


class GstValidateMediaCheckTest(Test):
    def __init__(self, classname, options, reporter, media_info_path, uri):
        super(GstValidateMediaCheckTest, self).__init__(DISCOVERER_COMMAND[0], classname,
                                              options, reporter,
                                              timeout=30)
        self._uri = uri
        self._media_info_path = urlparse.urlparse(media_info_path).path

    def build_arguments(self):
        self.add_arguments(self._uri, "--expected-results",
                           self._media_info_path)


class GstValidateTranscodingTest(GstValidateTest):
    def __init__(self, classname, options, reporter,
                 combination, uri, file_infos):

        super(GstValidateTranscodingTest, self).__init__(
            DEFAULT_GST_VALIDATE_TRANSCODING, classname,
            options, reporter, scenario=None)
        self.file_infos = file_infos
        self.uri = uri
        self.combination = combination
        self.dest_file = ""

    def set_rendering_info(self):
        self.dest_file = os.path.join(self.options.dest,
                                 os.path.basename(self.uri) +
                                 '-' + self.combination.acodec +
                                 self.combination.vcodec + '.' +
                                 self.combination.container)
        if urlparse.urlparse(self.dest_file).scheme == "":
            self.dest_file = path2url(self.dest_file)

        profile = get_profile(self.combination)
        self.add_arguments("-o", profile)

    def build_arguments(self):
        self.set_rendering_info()
        self.add_arguments(self.uri, self.dest_file)

    def get_current_value(self):
        return get_current_size(self)

    def check_results(self):
        if self.process.returncode == 0:
            orig_duration = long(self.file_infos.get("media-info", "file-duration"))
            res, msg = compare_rendered_with_original(orig_duration, self.dest_file)
            self.set_result(res, msg)
        else:
            GstValidateTest.check_results(self)


class GstValidateManager(TestsManager, Loggable):

    name = "validate"

    def __init__(self):
        TestsManager.__init__(self)
        Loggable.__init__(self)
        self._uris = []

    def init(self):
        if which(DEFAULT_GST_VALIDATE) and which(DEFAULT_GST_VALIDATE_TRANSCODING):
            return True

        return False

    def list_tests(self):
        SCENARIOS = ["none", "simple_backward",
                     "fast_forward", "seek_forward",
                     "seek_backward", "scrub_forward_seeking"]

        for test_pipeline in PLAYBACK_TESTS:
            name = "validate.playback"
            for scenario in SCENARIOS:
                self._add_playback_test(name, scenario, test_pipeline)

        for uri, mediainfo in self._list_uris():
            classname = "validate.media_check.%s" % (os.path.splitext(os.path.basename(uri))[0].replace(".", "_"))
            self.tests.append(GstValidateMediaCheckTest(classname,
                                                        self.options,
                                                        self.reporter,
                                                        mediainfo.path,
                                                        uri))

        for uri, mediainfo in self._list_uris():
            if mediainfo.config.getboolean("media-info", "is-image") is True:
                continue
            for comb in COMBINATIONS:
                classname = "validate.transcode.to_%s.%s" % (str(comb).replace(' ', '_'),
                                                             os.path.splitext(os.path.basename(uri))[0].replace(".", "_"))
                self.tests.append(GstValidateTranscodingTest(classname,
                                                             self.options,
                                                             self.reporter,
                                                             comb, uri,
                                                             mediainfo.config))

    def _check_discovering_info(self, media_info, uri=None):
        self.debug("Checking %s", media_info)
        config = ConfigParser.ConfigParser()
        f = open(media_info)
        config.readfp(f)
        try:
            # Just testing that the vairous mandatory infos are present
            caps = config.get("media-info", "caps")
            config.get("media-info", "file-duration")
            config.get("media-info", "seekable")
            if uri is None:
                uri = config.get("file-info", "uri")
            config.set("file-info", "protocol", urlparse.urlparse(uri).scheme)
            for caps2, prot in SPECIAL_PROTOCOLS:
                if caps2 == caps:
                    config.set("file-info", "protocol", prot)
                    break
            self._uris.append((uri,
                               NamedDic({"path": media_info,
                                         "config": config})))
        except ConfigParser.NoOptionError as e:
            self.debug("Exception: %s for %s", e, media_info)
        f.close()

    def _discover_file(self, uri, fpath):
        try:
            media_info = "%s.%s" % (fpath, MEDIA_INFO_EXT)
            args = list(DISCOVERER_COMMAND)
            args.append(uri)
            if os.path.isfile(media_info):
                self._check_discovering_info(media_info, uri)
                return True
            elif fpath.endswith(STREAM_INFO):
                self._check_discovering_info(fpath)
                return True
            elif self.options.generate_info:
                args.extend(["--output-file", media_info])
            else:
                return True

            subprocess.check_output(args)
            self._check_discovering_info(media_info, uri)

            return True

        except subprocess.CalledProcessError as e:
            self.debug("Exception: %s", e)
            return False

    def _list_uris(self):
        if self._uris:
            return self._uris

        if not self.args:
            if isinstance(self.options.paths, str):
                self.options.paths = [os.path.join(self.options.paths)]

            for path in self.options.paths:
                for root, dirs, files in os.walk(path):
                    for f in files:
                        fpath = os.path.join(path, root, f)
                        if os.path.isdir(fpath) or fpath.endswith(MEDIA_INFO_EXT):
                            continue
                        else:
                            self._discover_file(path2url(fpath), fpath)

        self.debug("Uris found: %s", self._uris)

        return self._uris

    def _get_fname(self, name, scenario, protocol=None):
        if protocol is not None:
            name = "%s.%s" % (name, protocol)

        if scenario is not None and scenario.lower() != "none":
            return "%s.%s" % (name, scenario)

        return name

    def _add_playback_test(self, name, scenario, pipe):
        if self.options.mute:
            if "autovideosink" in pipe:
                pipe = pipe.replace("autovideosink", "fakesink")
            if "autoaudiosink" in pipe:
                pipe = pipe.replace("autoaudiosink", "fakesink")

        if "__uri__" in pipe:
            for uri, minfo in self._list_uris():
                npipe = pipe
                if scenario != "none":
                    if minfo.config.getboolean("media-info", "seekable") is False:
                        self.debug("Do not run %s as %s does not support seeking",
                                   scenario, uri)
                        continue

                    if self.options.mute:
                        # In case of seeking we need to make sure the pipeline
                        # is run sync, otherwize some tests will fail
                        npipe = pipe.replace("fakesink", "'fakesink sync=true'")

                fname = "%s.%s" % (self._get_fname(name, scenario,
                                   minfo.config.get("file-info", "protocol")),
                                   os.path.basename(uri).replace(".", "_"))
                self.debug("Adding: %s", fname)

                self.tests.append(GstValidateLaunchTest(fname,
                                                  self.options,
                                                  self.reporter,
                                                  npipe.replace("__uri__", uri),
                                                  scenario=scenario,
                                                  file_infos=minfo.config)
                                 )
        else:
            self.debug("Adding: %s", name)
            self.tests.append(GstValidateLaunchTest(self._get_fname(fname, scenario),
                                              self.options,
                                              self.reporter,
                                              pipe,
                                              scenario=scenario))

    def needs_http_server(self):
        for uri, mediainfo in self._list_uris():
            if urlparse.urlparse(uri).scheme == "http" and \
                    "127.0.0.1:%s" % (self.options.http_server_port) in uri:
                return True
