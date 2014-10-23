#!/usr/bin/env python2
#
#       validate_default_testsuite.py
#
# Copyright (c) 2014, Thibault Saunier tsaunier@gnome.org
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


valid_mixing_scenarios=["play_15s",
                        "fast_forward",
                        "seek_forward",
                        "seek_backward",
                        "seek_with_stop",
                        "scrub_forward_seeking"]


def register_compositing_tests(self):
    """
    Those tests are not activated in the default testsuite,
    they should be activated in a configuration file.
    """
    for compositor in ["compositor", "glmixer"]:
        if gst_validate_checkout_element_present(compositor):
            self.add_generators(GstValidateMixerTestsGenerator(compositor, self,
                                                compositor,
                                                "video",
                                                converter="deinterlace ! videoconvert ! videorate ! videoscale ! video/x-raw,framerate=25/1,pixel-aspect-ratio=1/1",
                                                valid_scenarios=valid_mixing_scenarios))


def register_default_test_generators(self):
    """
    Registers default test generators
    """
    self.add_generators([GstValidatePlaybinTestsGenerator(self),
                         GstValidateMediaCheckTestsGenerator(self),
                         GstValidateTranscodingTestsGenerator(self)])

    for compositor in ["compositor", "glvideomixer"]:
            self.add_generators(GstValidateMixerTestsGenerator(compositor + ".simple", self,
                                                compositor,
                                                "video",
                                                converter="deinterlace ! videoconvert",
                                                mixed_srcs= {
                                                "synchronized": {"mixer_props": "sink_1::alpha=0.5 sink_1::xpos=50 sink_1::ypos=50",
                                                    "sources":
                                                       ("videotestsrc pattern=snow timestamp-offset=3000000000 ! 'video/x-raw,format=AYUV,width=640,height=480,framerate=(fraction)30/1' !  timeoverlay",
                                                        "videotestsrc pattern=smpte ! 'video/x-raw,format=AYUV,width=800,height=600,framerate=(fraction)10/1' ! timeoverlay")},
                                                "bgra":
                                                    ("videotestsrc ! video/x-raw, framerate=\(fraction\)10/1, width=100, height=100",
                                                     "videotestsrc ! video/x-raw, framerate=\(fraction\)5/1, width=320, height=240")
                                                    },
                                                valid_scenarios=valid_mixing_scenarios))


def register_default_scenarios(self):
    """
    Registers default test scenarios
    """
    self.add_scenarios([
                 "play_15s",
                 "reverse_playback",
                 "fast_forward",
                 "seek_forward",
                 "seek_backward",
                 "seek_with_stop",
                 "switch_audio_track",
                 "switch_audio_track_while_paused",
                 "switch_subtitle_track",
                 "switch_subtitle_track_while_paused",
                 "disable_subtitle_track_while_paused",
                 "change_state_intensive",
                 "scrub_forward_seeking"])

def register_default_encoding_formats(self):
    """
    Registers default encoding formats
    """
    self.add_encoding_formats([
        MediaFormatCombination("ogg", "vorbis", "theora"),
        MediaFormatCombination("webm", "vorbis", "vp8"),
        MediaFormatCombination("mp4", "mp3", "h264"),
        MediaFormatCombination("mkv", "vorbis", "h264"),
    ])

def register_default_blacklist(self):
    self.set_default_blacklist([
        # hls known issues
        ("validate.hls.playback.fast_forward.*",
         "https://bugzilla.gnome.org/show_bug.cgi?id=698155"),
        ("validate.hls.playback.seek_with_stop.*",
         "https://bugzilla.gnome.org/show_bug.cgi?id=723268"),
        ("validate.hls.playback.reverse_playback.*",
         "https://bugzilla.gnome.org/show_bug.cgi?id=702595"),
        ("validate.hls.*scrub_forward_seeking.*",
         "https://bugzilla.gnome.org/show_bug.cgi?id=606382"),
        ("validate.hls.*seek_backward.*",
         "https://bugzilla.gnome.org/show_bug.cgi?id=606382"),
        ("validate.hls.*seek_forward.*",
         "https://bugzilla.gnome.org/show_bug.cgi?id=606382"),

        # Matroska/WEBM known issues:
        ("validate.*.reverse_playback.*webm$",
         "https://bugzilla.gnome.org/show_bug.cgi?id=679250"),
        ("validate.*.reverse_playback.*mkv$",
         "https://bugzilla.gnome.org/show_bug.cgi?id=679250"),
        ("validate.*reverse.*Sintel_2010_720p_mkv",
         "TODO in matroskademux: FIXME: We should build an index during playback or "
         "when scanning that can be used here. The reverse playback code requires "
         " seek_index and seek_entry to be set!"),
        ("validate.http.playback.seek_with_stop.*webm",
         "matroskademux.gst_matroska_demux_handle_seek_push: Seek end-time not supported in streaming mode"),
        ("validate.http.playback.seek_with_stop.*mkv",
         "matroskademux.gst_matroska_demux_handle_seek_push: Seek end-time not supported in streaming mode"),

        # MPEG TS known issues:
        ('(?i)validate.*.playback.reverse_playback.*(?:_|.)(?:|m)ts$',
         "https://bugzilla.gnome.org/show_bug.cgi?id=702595"),
        ('validate.file.transcode.to_vorbis_and_vp8_in_webm.GH1_00094_1920x1280_MTS',
         'Got error: Internal data stream error. -- Debug message: mpegtsbase.c(1371):'
         'mpegts_base_loop (): ...: stream stopped, reason not-negotiated'),

        # HTTP known issues:
        ("validate.http.*scrub_forward_seeking.*", "This is not stable enough for now."),
        ("validate.http.playback.change_state_intensive.raw_video_mov",
         "This is not stable enough for now. (flow return from pad push doesn't match expected value)"),

        # MXF known issues"
        (".*reverse_playback.*mxf", "Reverse playback is not handled in MXF"),
        ("validate\.file\.transcode.*mxf", "FIXME: Transcoding and mixing tests need to be tested"),

        # Subtitles known issues
        ("validate.file.playback.switch_subtitle_track.Sintel_2010_720p_mkv", "https://bugzilla.gnome.org/show_bug.cgi?id=734051"),

        # Videomixing known issues
        ("validate.file.*.simple.scrub_forward_seeking.synchronized", "https://bugzilla.gnome.org/show_bug.cgi?id=734060"),

        # FLAC known issues"
        (".*reverse_playback.*flac", "Reverse playback is not handled in flac"),

        # WMV known issues"
        (".*reverse_playback.*wmv", "Reverse playback is not handled in wmv"),
        (".*reverse_playback.*asf", "Reverse playback is not handled in asf"),
    ])

def register_defaults(self):
    self.register_default_scenarios()
    self.register_default_encoding_formats()
    self.register_default_blacklist()
    self.register_default_test_generators()


def register_all(self):
    self.register_defaults()
    self.register_compositing_tests()


try:
    GstValidateTestManager.register_defaults = register_defaults
    GstValidateTestManager.register_all = register_all
    GstValidateTestManager.register_default_blacklist = register_default_blacklist
    GstValidateTestManager.register_default_test_generators = register_default_test_generators
    GstValidateTestManager.register_default_scenarios = register_default_scenarios
    GstValidateTestManager.register_compositing_tests = register_compositing_tests
    GstValidateTestManager.register_default_encoding_formats = register_default_encoding_formats
except NameError:
    pass
