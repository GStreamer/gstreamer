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

from baseclasses import GstValidateTest, TestsManager, Test, Scenario, NamedDic
from utils import MediaFormatCombination, get_profile,\
    path2url, DEFAULT_TIMEOUT, which, GST_SECOND, Result, \
    compare_rendered_with_original, Protocols


class PipelineDescriptor(object):
    def __init__(self, name, pipeline):
        self.name = name
        self._pipeline = pipeline

    def needs_uri(self):
        return False

    def get_pipeline(self, protocol=Protocols.FILE, uri=""):
        return self._pipeline


class PlaybinDescriptor(PipelineDescriptor):
    def __init__(self):
        PipelineDescriptor.__init__(self, "playbin", "playbin")

    def needs_uri(self):
        return True

    def get_pipeline(self, options, protocol, scenario, uri):
        pipe = self._pipeline
        if options.mute:
            fakesink = "fakesink"
            if scenario and scenario.seeks:
                fakesink = "'" + fakesink + " sync=true'"
            pipe += " audio-sink=%s video-sink=%s" %(fakesink, fakesink)

        pipe += " uri=%s" % uri

        if scenario.reverse and protocol == Protocols.HTTP:
            # 10MB so we can reverse playbacl
            pipe += " ring-buffer-max-size=10240"

        return pipe

# definitions of commands to use
GST_VALIDATE_COMMAND = "gst-validate-1.0"
GST_VALIDATE_TRANSCODING_COMMAND = "gst-validate-transcoding-1.0"
G_V_DISCOVERER_COMMAND = "gst-validate-media-check-1.0 --discover-only"

# Some extension file for discovering results
G_V_MEDIA_INFO_EXT = "media_info"
G_V_STREAM_INFO_EXT = "stream_info"

# Some info about protocols and how to handle them
G_V_CAPS_TO_PROTOCOL = [("application/x-hls", Protocols.HLS)]
G_V_PROTOCOL_TIMEOUTS = {Protocols.HTTP: 120,
                         Protocols.HLS: 240}

# Tests descriptions
G_V_PLAYBACK_TESTS = [PlaybinDescriptor()]

# Description of wanted output formats for transcoding test
G_V_ENCODING_TARGET_COMBINATIONS = [
    MediaFormatCombination("ogg", "vorbis", "theora"),
    MediaFormatCombination("webm", "vorbis", "vp8"),
    MediaFormatCombination("mp4", "mp3", "h264"),
    MediaFormatCombination("mkv", "vorbis", "h264")]


# List of scenarios to run depending on the protocol in use
G_V_SCENARIOS = {Protocols.FILE: [Scenario.get_scenario("play_15s"),
                                  Scenario.get_scenario("reverse_playback"),
                                  Scenario.get_scenario("fast_forward"),
                                  Scenario.get_scenario("seek_forward"),
                                  Scenario.get_scenario("seek_backward"),
                                  Scenario.get_scenario("seek_with_stop"),
                                  Scenario.get_scenario("scrub_forward_seeking")],
                 Protocols.HTTP: [Scenario.get_scenario("play_15s"),
                                  Scenario.get_scenario("fast_forward"),
                                  Scenario.get_scenario("seek_forward"),
                                  Scenario.get_scenario("seek_backward"),
                                  Scenario.get_scenario("seek_with_stop"),
                                  Scenario.get_scenario("reverse_playback")],
                 Protocols.HLS: [Scenario.get_scenario("play_15s"),
                                 Scenario.get_scenario("fast_forward"),
                                 Scenario.get_scenario("seek_forward"),
                                 Scenario.get_scenario("seek_with_stop"),
                                 Scenario.get_scenario("seek_backward")],
                 }

G_V_BLACKLISTED_TESTS = \
[("validate.hls.playback.fast_forward.*",
  "https://bugzilla.gnome.org/show_bug.cgi?id=698155"),
 ("validate.hls.playback.seek_with_stop.*",
  "https://bugzilla.gnome.org/show_bug.cgi?id=723268"),
 ("validate.*.reverse_playback.*webm$",
  "https://bugzilla.gnome.org/show_bug.cgi?id=679250"),
 ("validate.http.playback.seek_with_stop.*webm",
  "matroskademux.gst_matroska_demux_handle_seek_push: Seek end-time not supported in streaming mode"),
 ("validate.http.playback.seek_with_stop.*mkv",
  "matroskademux.gst_matroska_demux_handle_seek_push: Seek end-time not supported in streaming mode")
 ]

class GstValidateLaunchTest(GstValidateTest):
    def __init__(self, classname, options, reporter, pipeline_desc,
                 timeout=DEFAULT_TIMEOUT, scenario=None, file_infos=None):
        try:
            timeout = G_V_PROTOCOL_TIMEOUTS[file_infos.get("file-info", "protocol")]
        except KeyError:
            pass

        super(GstValidateLaunchTest, self).__init__(GST_VALIDATE_COMMAND, classname,
                                              options, reporter,
                                              scenario=scenario,
                                              timeout=timeout)

        self.pipeline_desc = pipeline_desc
        self.file_infos = file_infos

    def build_arguments(self):
        GstValidateTest.build_arguments(self)
        self.add_arguments(self.pipeline_desc)

    def get_current_value(self):
        return self.get_current_position()


class GstValidateMediaCheckTest(Test):
    def __init__(self, classname, options, reporter, media_info_path, uri, timeout=DEFAULT_TIMEOUT):
        super(GstValidateMediaCheckTest, self).__init__(G_V_DISCOVERER_COMMAND, classname,
                                              options, reporter,
                                              timeout=timeout)
        self._uri = uri
        self._media_info_path = urlparse.urlparse(media_info_path).path

    def build_arguments(self):
        self.add_arguments(self._uri, "--expected-results",
                           self._media_info_path)


class GstValidateTranscodingTest(GstValidateTest):
    def __init__(self, classname, options, reporter,
                 combination, uri, file_infos, timeout=DEFAULT_TIMEOUT,
                 scenario=Scenario.get_scenario("play_15s")):

        try:
            timeout = G_V_PROTOCOL_TIMEOUTS[file_infos.get("file-info", "protocol")]
        except KeyError:
            pass

        if scenario.max_duration is not None:
            hard_timeout = 4 * scenario.max_duration + timeout
        else:
            hard_timeout = None

        super(GstValidateTranscodingTest, self).__init__(
            GST_VALIDATE_TRANSCODING_COMMAND, classname,
            options, reporter, scenario=scenario, timeout=timeout,
            hard_timeout=hard_timeout)

        self.file_infos = file_infos
        self.uri = uri
        self.combination = combination
        self.dest_file = ""

    def set_rendering_info(self):
        self.dest_file = path = os.path.join(self.options.dest,
                                             self.classname.replace(".transcode.", os.sep).
                                             replace(".", os.sep))
        utils.mkdir(os.path.dirname(urlparse.urlsplit(self.dest_file).path))
        if urlparse.urlparse(self.dest_file).scheme == "":
            self.dest_file = path2url(self.dest_file)

        profile = get_profile(self.combination)
        self.add_arguments("-o", profile)

    def build_arguments(self):
        GstValidateTest.build_arguments(self)
        self.set_rendering_info()
        self.add_arguments(self.uri, self.dest_file)

    def get_current_value(self):
        return self.get_current_size()

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
        if which(GST_VALIDATE_COMMAND) and which(GST_VALIDATE_TRANSCODING_COMMAND):
            return True

        return False

    def list_tests(self):
        for test_pipeline in G_V_PLAYBACK_TESTS:
            self._add_playback_test(test_pipeline)

        for uri, mediainfo in self._list_uris():
            try:
                timeout = G_V_PROTOCOL_TIMEOUTS[mediainfo.config.get("file-info", "protocol")]
            except KeyError:
                timeout = DEFAULT_TIMEOUT

            classname = "validate.media_check.%s" % (os.path.basename(uri).replace(".", "_"))
            self.add_test(GstValidateMediaCheckTest(classname,
                                                    self.options,
                                                    self.reporter,
                                                    mediainfo.path,
                                                    uri,
                                                    timeout=timeout))

        for uri, mediainfo in self._list_uris():
            if mediainfo.config.getboolean("media-info", "is-image") is True:
                continue
            for comb in G_V_ENCODING_TARGET_COMBINATIONS:
                classname = "validate.%s.transcode.to_%s.%s" % (mediainfo.config.get("file-info", "protocol"),
                                                                str(comb).replace(' ', '_'),
                                                                os.path.basename(uri).replace(".", "_"))
                self.add_test(GstValidateTranscodingTest(classname,
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
            for caps2, prot in G_V_CAPS_TO_PROTOCOL:
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
            media_info = "%s.%s" % (fpath, G_V_MEDIA_INFO_EXT)
            args = G_V_DISCOVERER_COMMAND.split(" ")
            args.append(uri)
            if os.path.isfile(media_info):
                self._check_discovering_info(media_info, uri)
                return True
            elif fpath.endswith(G_V_STREAM_INFO_EXT):
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
                        if os.path.isdir(fpath) or fpath.endswith(G_V_MEDIA_INFO_EXT):
                            continue
                        else:
                            self._discover_file(path2url(fpath), fpath)

        self.debug("Uris found: %s", self._uris)

        return self._uris

    def _get_fname(self, scenario, protocol=None):
        if scenario is not None and scenario.name.lower() != "none":
            return "%s.%s.%s.%s" % ("validate", protocol, "playback", scenario.name)

        return "%s.%s.%s" % ("validate", protocol, "playback")

    def _add_playback_test(self, pipe_descriptor):
        if pipe_descriptor.needs_uri():
            for uri, minfo in self._list_uris():
                protocol = minfo.config.get("file-info", "protocol")
                for scenario in G_V_SCENARIOS[protocol]:
                    npipe = pipe_descriptor.get_pipeline(self.options,
                                                         protocol,
                                                         scenario, uri)
                    if minfo.config.getboolean("media-info", "seekable") is False:
                        self.debug("Do not run %s as %s does not support seeking",
                                   scenario, uri)
                        continue

                    fname = "%s.%s" % (self._get_fname(scenario,
                                       protocol),
                                       os.path.basename(uri).replace(".", "_"))
                    self.debug("Adding: %s", fname)

                    self.add_test(GstValidateLaunchTest(fname,
                                                        self.options,
                                                        self.reporter,
                                                        npipe,
                                                        scenario=scenario,
                                                        file_infos=minfo.config)
                                 )
        else:
            self.add_test(GstValidateLaunchTest(self._get_fname(scenario, "testing"),
                                                self.options,
                                                self.reporter,
                                                pipe_descriptor.get_pipeline(self.options),
                                                scenario=scenario))

    def needs_http_server(self):
        for uri, mediainfo in self._list_uris():
            if urlparse.urlparse(uri).scheme == Protocols.HTTP and \
                    "127.0.0.1:%s" % (self.options.http_server_port) in uri:
                return True

    def get_blacklisted(self):
        return G_V_BLACKLISTED_TESTS
