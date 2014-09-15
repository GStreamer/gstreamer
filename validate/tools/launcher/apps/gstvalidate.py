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
import os
import sys
import time
import urlparse
import subprocess
import ConfigParser
from loggable import Loggable

from baseclasses import GstValidateTest, TestsManager, Test, \
    ScenarioManager, NamedDic, GstValidateTestsGenerator, \
    GstValidateMediaDescriptor, GstValidateEncodingTestInterface, \
    GstValidateBaseTestManager

from utils import path2url, DEFAULT_TIMEOUT, which, \
    GST_SECOND, Result, Protocols

######################################
#       Private global variables     #
######################################

# definitions of commands to use
GST_VALIDATE_COMMAND = "gst-validate-1.0"
GST_VALIDATE_TRANSCODING_COMMAND = "gst-validate-transcoding-1.0"
G_V_DISCOVERER_COMMAND = "gst-validate-media-check-1.0"
if "win32" in sys.platform:
    GST_VALIDATE_COMMAND += ".exe"
    GST_VALIDATE_TRANSCODING_COMMAND += ".exe"
    G_V_DISCOVERER_COMMAND += ".exe"

AUDIO_ONLY_FILE_TRANSCODING_RATIO = 5

#################################################
#       API to be used to create testsuites     #
#################################################

"""
Some info about protocols and how to handle them
"""
GST_VALIDATE_CAPS_TO_PROTOCOL = [("application/x-hls", Protocols.HLS),
                                 ("application/dash+xml", Protocols.DASH)]
GST_VALIDATE_PROTOCOL_TIMEOUTS = {Protocols.HTTP: 120,
                                  Protocols.HLS: 240,
                                  Protocols.DASH: 240}


class GstValidateMediaCheckTestsGenerator(GstValidateTestsGenerator):
    def __init__(self, test_manager):
        GstValidateTestsGenerator.__init__(self, "media_check", test_manager)

    def populate_tests(self, uri_minfo_special_scenarios, scenarios):
        for uri, mediainfo, special_scenarios in uri_minfo_special_scenarios:
            protocol = mediainfo.media_descriptor.get_protocol()
            try:
                timeout = GST_VALIDATE_PROTOCOL_TIMEOUTS[protocol]
            except KeyError:
                timeout = DEFAULT_TIMEOUT

            classname = "validate.%s.media_check.%s" % (protocol,
                                                        os.path.basename(uri).replace(".", "_"))
            self.add_test(GstValidateMediaCheckTest(classname,
                                                    self.test_manager.options,
                                                    self.test_manager.reporter,
                                                    mediainfo.media_descriptor,
                                                    uri,
                                                    mediainfo.path,
                                                    timeout=timeout))


class GstValidateTranscodingTestsGenerator(GstValidateTestsGenerator):
    def __init__(self, test_manager):
        GstValidateTestsGenerator.__init__(self, "transcode", test_manager)

    def populate_tests(self, uri_minfo_special_scenarios, scenarios):
        for uri, mediainfo, special_scenarios in uri_minfo_special_scenarios:
            if mediainfo.media_descriptor.is_image():
                continue

            for comb in self.test_manager.get_encoding_formats():
                classname = "validate.%s.transcode.to_%s.%s" % (mediainfo.media_descriptor.get_protocol(),
                                                                str(comb).replace(' ', '_'),
                                                                mediainfo.media_descriptor.get_clean_name())
                self.add_test(GstValidateTranscodingTest(classname,
                                                         self.test_manager.options,
                                                         self.test_manager.reporter,
                                                         comb,
                                                         uri,
                                                         mediainfo.media_descriptor))


class GstValidatePipelineTestsGenerator(GstValidateTestsGenerator):
    def __init__(self, name, test_manager, pipeline_template=None, pipelines_descriptions=None,
                 valid_scenarios=[]):
        """
        @name: The name of the generator
        @pipeline_template: A template pipeline to be used to generate actual pipelines
        @pipelines_descriptions: A list of tuple of the form:
                                 (test_name, pipeline_description)
        @valid_scenarios: A list of scenario name that can be used with that generator
        """
        GstValidateTestsGenerator.__init__(self, name, test_manager)
        self._pipeline_template = pipeline_template
        self._pipelines_descriptions = pipelines_descriptions
        self._valid_scenarios = valid_scenarios

    def get_fname(self, scenario, protocol=None, name=None):
        if name is None:
            name = self.name

        if protocol is not None:
            protocol_str = "%s." % protocol
        else:
            protocol_str = ""

        if scenario is not None and scenario.name.lower() != "none":
            return "%s.%s%s.%s" % ("validate", protocol_str, name, scenario.name)

        return "%s.%s%s" % ("validate", protocol_str, name)

    def generate_tests(self, uri_minfo_special_scenarios, scenarios):

        if self._valid_scenarios:
            scenarios = [scenario for scenario in scenarios if
                          scenario.name in self._valid_scenarios]

        return super(GstValidatePipelineTestsGenerator, self).generate_tests(
              uri_minfo_special_scenarios, scenarios)

    def populate_tests(self, uri_minfo_special_scenarios, scenarios):
        for name, pipeline in self._pipelines_descriptions:
            for scenario in scenarios:
                fname = self.get_fname(scenario, name=name)
                self.add_test(GstValidateLaunchTest(fname,
                                                    self.test_manager.options,
                                                    self.test_manager.reporter,
                                                    pipeline,
                                                    scenario=scenario)
                              )


class GstValidatePlaybinTestsGenerator(GstValidatePipelineTestsGenerator):

    def __init__(self, test_manager):
        GstValidatePipelineTestsGenerator.__init__(self, "playback", test_manager, "playbin")

    def populate_tests(self, uri_minfo_special_scenarios, scenarios):
        for uri, minfo, special_scenarios in uri_minfo_special_scenarios:
            pipe = self._pipeline_template
            protocol = minfo.media_descriptor.get_protocol()

            pipe += " uri=%s" % uri
            for scenario in special_scenarios + scenarios:
                cpipe = pipe
                if not minfo.media_descriptor.is_compatible(scenario):
                    continue

                if self.test_manager.options.mute:
                    if scenario.needs_clock_sync():
                        fakesink = "'fakesink sync=true'"
                    else:
                        fakesink = "'fakesink'"

                    cpipe += " audio-sink=%s video-sink=%s" %(fakesink, fakesink)

                fname = "%s.%s" % (self.get_fname(scenario,
                                   protocol),
                                   os.path.basename(minfo.media_descriptor.get_clean_name()))
                self.debug("Adding: %s", fname)

                if scenario.does_reverse_playback() and protocol == Protocols.HTTP:
                    # 10MB so we can reverse playback
                    cpipe += " ring-buffer-max-size=10485760"

                self.add_test(GstValidateLaunchTest(fname,
                                                    self.test_manager.options,
                                                    self.test_manager.reporter,
                                                    cpipe,
                                                    scenario=scenario,
                                                    media_descriptor=minfo.media_descriptor)
                              )


class GstValidateMixerTestsGenerator(GstValidatePipelineTestsGenerator):
    def __init__(self, name, test_manager, mixer, media_type, converter="", num_sources=3,
                 mixed_srcs={}, valid_scenarios=[]):
        pipe_template = "%(mixer)s name=_mixer !  " + converter + " ! %(sink)s "
        self.converter = converter
        self.mixer = mixer
        self.media_type = media_type
        self.num_sources = num_sources
        self.mixed_srcs = mixed_srcs
        super(GstValidateMixerTestsGenerator, self).__init__(name, test_manager, pipe_template,
                                                             valid_scenarios=valid_scenarios)

    def populate_tests(self, uri_minfo_special_scenarios, scenarios):
        wanted_ressources = []
        for uri, minfo, special_scenarios in uri_minfo_special_scenarios:
            protocol = minfo.media_descriptor.get_protocol()
            if protocol == Protocols.FILE and \
                    minfo.media_descriptor.get_num_tracks(self.media_type) > 0:
                wanted_ressources.append((uri, minfo))

        if not self.mixed_srcs:
            if not wanted_ressources:
                return

            for i in range(len(uri_minfo_special_scenarios) / self.num_sources):
                can_run = True
                srcs = []
                name = ""
                for nsource in range(self.num_sources):
                    uri, minfo = wanted_ressources[i + nsource]
                    srcs.append("uridecodebin uri=%s ! %s" % (uri, self.converter))
                    fname = os.path.basename(uri).replace(".", "_")
                    if not name:
                        name = fname
                    else:
                        name += "+%s" % fname

                self.mixed_srcs[name] = tuple(srcs)

        for name, srcs in self.mixed_srcs.iteritems():
            if isinstance(srcs, dict):
                pipe_arguments = {"mixer": self.mixer + " %s" % srcs["mixer_props"]}
                srcs = srcs["sources"]
            else:
                pipe_arguments = {"mixer": self.mixer}

            if self.test_manager.options.mute:
                pipe_arguments["sink"] = "'fakesink'"
            else:
                pipe_arguments["sink"] = "auto%ssink" % self.media_type

            pipe = self._pipeline_template % pipe_arguments

            for src in srcs:
                pipe += "%s ! _mixer. " % src

            for scenario in scenarios:
                fname = self.get_fname(scenario, Protocols.FILE) + "."
                fname += name

                self.debug("Adding: %s", fname)

                self.add_test(GstValidateLaunchTest(fname,
                                                    self.test_manager.options,
                                                    self.test_manager.reporter,
                                                    pipe,
                                                    scenario=scenario)
                              )


class GstValidateLaunchTest(GstValidateTest):
    def __init__(self, classname, options, reporter, pipeline_desc,
                 timeout=DEFAULT_TIMEOUT, scenario=None, media_descriptor=None):
        try:
            timeout = GST_VALIDATE_PROTOCOL_TIMEOUTS[media_descriptor.get_protocol()]
        except KeyError:
            pass
        except AttributeError:
            pass

        duration = 0
        if scenario:
            duration = scenario.get_duration()
        elif media_descriptor:
            duration = media_descriptor.get_duration() / GST_SECOND
        super(GstValidateLaunchTest, self).__init__(GST_VALIDATE_COMMAND, classname,
                                              options, reporter,
                                              duration=duration,
                                              scenario=scenario,
                                              timeout=timeout)

        self.pipeline_desc = pipeline_desc
        self.media_descriptor = media_descriptor

    def build_arguments(self):
        GstValidateTest.build_arguments(self)
        self.add_arguments(self.pipeline_desc)

    def get_current_value(self):
        if self.scenario:
            sent_eos = self.sent_eos_position()
            if sent_eos is not None:
                t = time.time()
                if ((t - sent_eos)) > 30:
                    if self.media_descriptor.get_protocol() == Protocols.HLS:
                        self.set_result(Result.PASSED,
                                        """Got no EOS 30 seconds after sending EOS,
                                        in HLS known and tolerated issue:
                                        https://bugzilla.gnome.org/show_bug.cgi?id=723868""")
                        return Result.KNOWN_ERROR

                    self.set_result(Result.FAILED, "Pipeline did not stop 30 Seconds after sending EOS")

                    return Result.FAILED

        return self.get_current_position()


class GstValidateMediaCheckTest(Test):
    def __init__(self, classname, options, reporter, media_descriptor, uri, minfo_path,
                 timeout=DEFAULT_TIMEOUT):
        super(GstValidateMediaCheckTest, self).__init__(G_V_DISCOVERER_COMMAND, classname,
                                              options, reporter,
                                              timeout=timeout)
        self._uri = uri
        self.media_descriptor = media_descriptor
        self._media_info_path = minfo_path

    def build_arguments(self):
        self.add_arguments(self._uri, "--expected-results",
                           self._media_info_path)


class GstValidateTranscodingTest(GstValidateTest, GstValidateEncodingTestInterface):
    scenarios_manager = ScenarioManager()
    def __init__(self, classname, options, reporter,
                 combination, uri, media_descriptor,
                 timeout=DEFAULT_TIMEOUT,
                 scenario=None):

        Loggable.__init__(self)

        file_dur = long(media_descriptor.get_duration()) / GST_SECOND
        if not media_descriptor.get_num_tracks("video"):
            self.debug("%s audio only file applying transcoding ratio."
                       "File 'duration' : %s" % (classname , file_dur))
            duration = file_dur / AUDIO_ONLY_FILE_TRANSCODING_RATIO
        else:
            duration = file_dur

        try:
            timeout = GST_VALIDATE_PROTOCOL_TIMEOUTS[media_descriptor.get_protocol()]
        except KeyError:
            pass

        super(GstValidateTranscodingTest, self).__init__(GST_VALIDATE_TRANSCODING_COMMAND,
                                                         classname,
                                                         options,
                                                         reporter,
                                                         duration=duration,
                                                         timeout=timeout,
                                                         scenario=scenario)

        GstValidateEncodingTestInterface.__init__(self, combination, media_descriptor)

        self.media_descriptor = media_descriptor
        self.uri = uri

    def set_rendering_info(self):
        self.dest_file = path = os.path.join(self.options.dest,
                                             self.classname.replace(".transcode.", os.sep).
                                             replace(".", os.sep))
        utils.mkdir(os.path.dirname(urlparse.urlsplit(self.dest_file).path))
        if urlparse.urlparse(self.dest_file).scheme == "":
            self.dest_file = path2url(self.dest_file)

        profile = self.get_profile()
        self.add_arguments("-o", profile)

    def build_arguments(self):
        GstValidateTest.build_arguments(self)
        self.set_rendering_info()
        self.add_arguments(self.uri, self.dest_file)

    def get_current_value(self):
        if self.scenario:
            sent_eos = self.sent_eos_position()
            if sent_eos is not None:
                t = time.time()
                if ((t - sent_eos)) > 30:
                    if self.media_descriptor.get_protocol() == Protocols.HLS:
                        self.set_result(Result.PASSED,
                                        """Got no EOS 30 seconds after sending EOS,
                                        in HLS known and tolerated issue:
                                        https://bugzilla.gnome.org/show_bug.cgi?id=723868""")
                        return Result.KNOWN_ERROR

                    self.set_result(Result.FAILED, "Pipeline did not stop 30 Seconds after sending EOS")

                    return Result.FAILED

        size = self.get_current_size()
        if size is None:
            return self.get_current_position()

        return size

    def check_results(self):
        if self.result in [Result.FAILED, Result.TIMEOUT] or \
                self.process.returncode != 0:
            GstValidateTest.check_results(self)
            return

        if self.scenario:
            orig_duration = min(long(self.scenario.get_duration()),
                                long(self.media_descriptor.get_duration()))
        else:
            orig_duration = long(self.media_descriptor.get_duration())

        res, msg = self.check_encoded_file()
        self.set_result(res, msg)


class GstValidateTestManager(GstValidateBaseTestManager):

    name = "validate"

    # List of all classes to create testsuites
    GstValidateMediaCheckTestsGenerator = GstValidateMediaCheckTestsGenerator
    GstValidateTranscodingTestsGenerator = GstValidateTranscodingTestsGenerator
    GstValidatePipelineTestsGenerator = GstValidatePipelineTestsGenerator
    GstValidatePlaybinTestsGenerator = GstValidatePlaybinTestsGenerator
    GstValidateMixerTestsGenerator = GstValidateMixerTestsGenerator
    GstValidateLaunchTest = GstValidateLaunchTest
    GstValidateMediaCheckTest = GstValidateMediaCheckTest
    GstValidateTranscodingTest = GstValidateTranscodingTest

    def __init__(self):
        super(GstValidateTestManager, self).__init__()
        self._uris = []
        self._run_defaults = True
        self._is_populated = False
        execfile(os.path.join(os.path.dirname(__file__), "apps",
                 "validate", "validate_testsuite.py"), globals())

    def init(self):
        if which(GST_VALIDATE_COMMAND) and which(GST_VALIDATE_TRANSCODING_COMMAND):
            return True
        return False

    def add_options(self, parser):
        group = parser.add_argument_group("GstValidate tools specific options"
                            " and behaviours",
description="""When using --wanted-tests, all the scenarios can be used, even those which have
not been tested and explicitely activated if you set use --wanted-tests ALL""")

    def populate_testsuite(self):

        if self._is_populated is True:
            return

        if not self.options.config:
            if self._run_defaults:
                self.register_defaults()
            else:
                self.register_all()

        self._is_populated = True

    def list_tests(self):
        if self.tests:
            return self.tests

        if self._run_defaults:
            scenarios = [self.scenarios_manager.get_scenario(scenario_name)
                         for scenario_name in self.get_scenarios()]
        else:
            scenarios = self.scenarios_manager.get_scenario(None)
        uris = self._list_uris()

        for generator in self.get_generators():
            for test in generator.generate_tests(uris, scenarios):
                self.add_test(test)

        return self.tests

    def _add_media(self, media_info, uri=None):
        self.debug("Checking %s", media_info)
        if isinstance(media_info, GstValidateMediaDescriptor):
            media_descriptor = media_info
            media_info = media_descriptor.get_path()
        else:
            media_descriptor = GstValidateMediaDescriptor(media_info)

        try:
            # Just testing that the vairous mandatory infos are present
            caps = media_descriptor.get_caps()
            if uri is None:
                uri = media_descriptor.get_uri()

            media_descriptor.set_protocol(urlparse.urlparse(uri).scheme)
            for caps2, prot in GST_VALIDATE_CAPS_TO_PROTOCOL:
                if caps2 == caps:
                    media_descriptor.set_protocol(prot)
                    break

            scenario_bname = media_descriptor.get_media_filepath()
            special_scenarios = self.scenarios_manager.find_special_scenarios(scenario_bname)
            self._uris.append((uri,
                               NamedDic({"path": media_info,
                                         "media_descriptor": media_descriptor}),
                               special_scenarios))
        except ConfigParser.NoOptionError as e:
            self.debug("Exception: %s for %s", e, media_info)

    def _discover_file(self, uri, fpath):
        try:
            media_info = "%s.%s" % (fpath, GstValidateMediaDescriptor.MEDIA_INFO_EXT)
            args = G_V_DISCOVERER_COMMAND.split(" ")
            args.append(uri)
            if os.path.isfile(media_info):
                self._add_media(media_info, uri)
                return True
            elif fpath.endswith(GstValidateMediaDescriptor.STREAM_INFO_EXT):
                self._add_media(fpath)
                return True
            elif not self.options.generate_info:
                return True

            media_descriptor = GstValidateMediaDescriptor.new_from_uri(uri, True,
                                                                       self.options.generate_info_full)
            if media_descriptor:
                self._add_media(media_descriptor, uri)
            else:
                self.warning("Could not get any descriptor for %s" % uri)

            return True

        except subprocess.CalledProcessError as e:
            if self.options.generate_info:
                printc("Result: Failed", Colors.FAIL)
            else:
                self.error("Exception: %s", e)
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
                        if os.path.isdir(fpath) or \
                                fpath.endswith(GstValidateMediaDescriptor.MEDIA_INFO_EXT) or\
                                fpath.endswith(ScenarioManager.FILE_EXTENDION):
                            continue
                        else:
                            self._discover_file(path2url(fpath), fpath)

        self.debug("Uris found: %s", self._uris)

        return self._uris

    def needs_http_server(self):
        for test in self.list_tests():
            if self._is_test_wanted(test) and test.media_descriptor is not None:
                protocol = test.media_descriptor.get_protocol()
                uri = test.media_descriptor.get_uri()

                if protocol in [Protocols.HTTP, Protocols.HLS, Protocols.DASH] and \
                    "127.0.0.1:%s" % (self.options.http_server_port) in uri:
                    return True
        return False

    def set_settings(self, options, args, reporter):
        if options.wanted_tests:
            for i in range(len(options.wanted_tests)):
                if "ALL" in options.wanted_tests[i]:
                    self._run_defaults = False
                    options.wanted_tests[i] = options.wanted_tests[i].replace("ALL", "")
        try:
            options.wanted_tests.remove("")
        except ValueError:
            pass

        super(GstValidateTestManager, self).set_settings(options, args, reporter)

def gst_validate_checkout_element_present(element_name):
    null = open(os.devnull)
    return subprocess.call("gst-inspect-1.0 videmixer", shell=True, stdout=null, stderr=null)
