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


def register_default_test_generators(self):
    """
    Registers default test generators
    """
    self.add_generators([GstValidatePlaybinTestsGenerator(self),
                         GstValidateMediaCheckTestsGenerator(self),
                         GstValidateTranscodingTestsGenerator(self)])


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
        ("validate.hls.playback.fast_forward.*",
         "https://bugzilla.gnome.org/show_bug.cgi?id=698155"),
        ("validate.hls.playback.seek_with_stop.*",
         "https://bugzilla.gnome.org/show_bug.cgi?id=723268"),
        ("validate.hls.playback.reverse_playback.*",
         "https://bugzilla.gnome.org/show_bug.cgi?id=702595"),
        ("validate.hls.*scrub_forward_seeking.*", "This is not stable enough for now."),

        # Matroska/WEBM known issues:
        ("validate.*.reverse_playback.*webm$",
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

        # HTTP known issues:
        ("validate.http.*scrub_forward_seeking.*", "This is not stable enough for now."),
    ])

def register_defaults(self):
    self.register_default_scenarios()
    self.register_default_encoding_formats()
    self.register_default_blacklist()
    self.register_default_test_generators()

try:
    GstValidateTestManager.register_defaults = register_defaults
    GstValidateTestManager.register_default_blacklist = register_default_blacklist
    GstValidateTestManager.register_default_test_generators = register_default_test_generators
    GstValidateTestManager.register_default_scenarios = register_default_scenarios
    GstValidateTestManager.register_compositing_tests = register_compositing_tests
    GstValidateTestManager.register_default_encoding_formats = register_default_encoding_formats
except NameError:
    pass
