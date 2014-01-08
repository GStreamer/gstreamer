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
import subprocess
import urlparse
import urllib
import ConfigParser
from loggable import Loggable

from testdefinitions import Test, TestsManager, DEFAULT_TIMEOUT


DEFAULT_GST_VALIDATE = "gst-validate-1.0"
DISCOVERER_COMMAND = ["gst-validate-media-check-1.0"]
MEDIA_INFO_EXT = "media_info"
STREAM_INFO = "stream_info"

SEEKING_REQUIERED_SCENARIO=["seek_forward", "seek_backward", "scrub_forward_seeking"]
SPECIAL_PROTOCOLS = [("application/x-hls", "hls")]

all_tests = {
    "playback":
    {
        "pipelines": ["playbin uri=__uri__ audio_sink=autoaudiosink video_sink=autovideosink"],
        "scenarios": ["none", "seek_forward", "seek_backward", "scrub_forward_seeking"]
    },
}


def path2url(path):
    return urlparse.urljoin('file:', urllib.pathname2url(path))


class GstValidateTest(Test):
    def __init__(self,
                 classname,
                 options,
                 reporter,
                 pipeline_desc,
                 scenario,
                 file_infos=None,
                 timeout=DEFAULT_TIMEOUT):
        if file_infos is not None:
            timeout = file_infos.get("media-info", "file-duration")

        super(GstValidateTest, self).__init__(DEFAULT_GST_VALIDATE, classname,
                                              options, reporter, scenario,
                                              timeout)
        self.pipeline_desc = pipeline_desc
        self.file_infos = file_infos

    def build_arguments(self):
        Test.build_arguments(self)
        self.add_arguments(self.pipeline_desc)


class GstValidateManager(TestsManager, Loggable):

    name = "validate"

    def __init__(self):
        TestsManager.__init__(self)
        Loggable.__init__(self)
        self._uris = []

    def add_options(self, group):
        group.add_option("-c", "--check-discovering", dest="check_discovering",
                         default=False, action="store_true",
                         help="Whether to check discovering results using %s"
                         % DISCOVERER_COMMAND[0])

    def list_tests(self):
        for mainname, tests in all_tests.iteritems():
            name = "validate.%s" % (mainname)
            for pipe in tests["pipelines"]:
                for scenario in tests["scenarios"]:
                    self._add_test(name, scenario, pipe)

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
            self._uris.append((uri, config))
        except ConfigParser.NoOptionError as e:
            self.debug("Exception: %s for %s", e, media_info)
            pass
        f.close()

    def _discover_file(self, uri, fpath):
        try:
            media_info = "%s.%s" % (fpath, MEDIA_INFO_EXT)
            args = list(DISCOVERER_COMMAND)
            args.append(uri)
            if os.path.isfile(media_info):
                if self.options.check_discovering is False:
                    self._check_discovering_info(media_info, uri)
                    return True
                else:
                    args.extend(["--expected-results", media_info])
            else:
                args.extend(["--output-file", media_info])

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
                        elif fpath.endswith(STREAM_INFO):
                            self._check_discovering_info(fpath)
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
            for uri, config in self._list_uris():
                npipe = pipe
                if scenario in SEEKING_REQUIERED_SCENARIO:
                    if config.getboolean("media-info", "seekable") is False:
                        self.debug("Do not run %s as %s does not support seeking",
                                    scenario, uri)
                        continue

                    if self.options.mute:
                        # In case of seeking we need to make sure the pipeline
                        # is run sync, otherwize some tests will fail
                        npipe = pipe.replace("fakesink", "'fakesink sync=true'")

                fname = "%s.%s" % (self._get_fname(name, scenario,
                                   config.get("file-info", "protocol")),
                                   os.path.basename(uri).replace(".", "_"))
                self.debug("Adding: %s", fname)

                self.tests.append(GstValidateTest(fname,
                                                  self.options,
                                                  self.reporter,
                                                  npipe.replace("__uri__", uri),
                                                  scenario,
                                                  config)
                                 )
        else:
            self.debug("Adding: %s", name)
            self.tests.append(GstValidateTest(self._get_fname(fname, scenario),
                                              self.options,
                                              self.reporter,
                                              pipe,
                                              scenario))
