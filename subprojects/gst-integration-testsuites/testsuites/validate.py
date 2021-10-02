# -*- Mode: Python -*- vi:si:et:sw=4:sts=4:ts=4:syntax=python
#
# Copyright (c) 2014,Thibault Saunier <thibault.saunier@collabora.com>
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

"""
The GstValidate default testsuite
"""

import os
import glob
import pathlib
import re
import subprocess

from testsuiteutils import update_assets
from launcher import utils
from launcher.baseclasses import MediaFormatCombination
from launcher.apps.gstvalidate import GstValidateSimpleTestsGenerator
from validate_known_issues import KNOWN_ISSUES


TEST_MANAGER = "validate"

BLACKLIST = [('validate.file.transcode.to_vorbis_and_vp8_in_webm.GH1_00094_1920x1280_MTS',
              'Got error: Internal data stream error. -- Debug message: mpegtsbase.c(1371):'
              'mpegts_base_loop (): ...: stream stopped, reason not-negotiated'),
             ('validate.testbin.transcode.*',
              "Encoding testsrc is not so interesting and the source is now unlimited"),
             ('validate.file.*.simple.fast_forward.synchronized',
              'https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/541'),
             ('validate.hls.playback.change_state_intensive.*',
              'https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/482'),
            ('validate.rtsp.*playback.switch.*',
             'https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/357'),
            ('validate.rtsp.*playback.*seek.*mxf$|validate.rtsp.*playback.*change_state_intensive.*mxf$',
             'Actions on MXF streams with rtsp-server fail in racy ways.'
             ' (Deactivating as it is not very important.)'),
            ('validate.rtsp.*pal-dv25_mxf$',
             'File has decoding issues with rtsp-server.'
             ' (Deactivating as it is not very important.)'),
             ("(?!.*.media_check.qtdemux-test-frag-basic_zero_dur_no_mehd_mp4).*.qtdemux-test-frag-basic_zero_dur_no_mehd_mp4",
              '`qtdemux-test-frag-basic_zero_dur_no_mehd_mp4` is there only for media_check tests.'),
             ('validate.rtsp.*playback.*mp3_h265_0_mp4$',
              'The version of libav shipped by Fedora 29 crashes in various ways with these tests.'),
             ('validate.rtsp.*playback.seek.*GH1_00094_1920x1280_MTS',
              'Do not preroll after pause.'),
             ('validate.file.playback.reverse_playback.sample_mpeg_program_stream_scr_mpg',
              'Do not decode any frame in reverse playback with SCR.'),
             ('validate.rtsp.*playback.seek.*sample_mpeg_program_stream_scr_mpg',
              'Racy with CI. No frames decoded before the end of the stream.'),
             ]

def add_accurate_seek_tests(test_manager, media_dir, extra_data):
    accurate_seeks_media_infos = []
    for f in [
            'mp4/timecoded_jpeg_23976fps.mp4.media_info.skipped',
            'mp4/timecoded_jpeg_2997fps.mp4.media_info.skipped',
            'mp4/timecoded_jpeg_30fps.mp4.media_info.skipped',

            'mp4/timecoded_vp8_23976fps.mp4.media_info.skipped',
            'mp4/timecoded_vp8_2997fps.mp4.media_info.skipped',
            'mp4/timecoded_vp8_30fps.mp4.media_info.skipped',

            'mp4/timecoded_h264_23976fps.mp4.media_info.skipped',
            'mp4/timecoded_h264_2997fps.mp4.media_info.skipped',
            'mp4/timecoded_h264_30fps.mp4.media_info.skipped',
        ]:
        dirname = os.path.join(media_dir, "defaults", os.path.dirname(f))
        filename = os.path.basename(f)
        media_info = os.path.join(dirname, filename)
        reference_frames_dir = os.path.join(dirname, re.sub(r"\.media_info.*", "_reference_frames", filename).replace('.', '_'))
        accurate_seeks_media_infos.append((media_info, reference_frames_dir))

    test_manager.add_generators(
        test_manager.GstValidateCheckAccurateSeekingTestGenerator(
            'accurate_seeks',
            test_manager,
            [(os.path.join(media_dir, media_info), os.path.join(media_dir, reference_frames_dir)) for media_info, reference_frames_dir in accurate_seeks_media_infos],
            extra_data=extra_data)
    )


def setup_tests(test_manager, options):
    testsuite_dir = os.path.realpath(os.path.join(os.path.dirname(__file__)))
    media_dir = os.path.realpath(os.path.join(testsuite_dir, os.path.pardir, "medias"))

    assets_dir = os.path.realpath(os.path.join(testsuite_dir, os.path.pardir, "medias", "defaults"))
    if options.sync:
        if not utils.USING_SUBPROJECT:
            if not update_assets(options, assets_dir):
                return False
        else:
            print("Syncing gst-integration-testsuites media files")
            subprocess.check_call(['git', 'submodule', 'update', '--init'],
                                cwd=utils.DEFAULT_GST_QA_ASSETS)
            subprocess.check_call(['git', 'lfs', 'pull', '--exclude='],
                                cwd=pathlib.Path(utils.DEFAULT_GST_QA_ASSETS) / 'medias')

    options.add_paths(assets_dir)
    options.set_http_server_dir(media_dir)
    test_manager.set_default_blacklist(BLACKLIST)

    extra_data = {
        "config_path": os.path.dirname(testsuite_dir),
        "medias": media_dir,
        "validate-flow-expectations-dir": os.path.join(testsuite_dir, "validate", "flow-expectations"),
        "validate-flow-actual-results-dir": test_manager.options.logsdir,
        "ssim-results-dir": os.path.join(test_manager.options.logsdir, "ssim-results"),
    }
    add_accurate_seek_tests(test_manager, media_dir, extra_data)

    test_manager.add_generators(
        GstValidateSimpleTestsGenerator("simple", test_manager,
            os.path.join(testsuite_dir, "validate"))
    )

    test_manager.add_expected_issues(KNOWN_ISSUES)
    test_manager.register_defaults()

    test_manager.add_encoding_formats([MediaFormatCombination("quicktime", "rawaudio", "prores")])

    valid_mixing_scenarios = ["play_15s",
                              "fast_forward",
                              "seek_forward",
                              "seek_backward",
                              "seek_with_stop",
                              "scrub_forward_seeking"]

    for compositor in ["compositor", "glvideomixer"]:
        test_manager.add_generators(
            test_manager.GstValidateMixerTestsGenerator(compositor + ".simple", test_manager,
                                                        compositor,
                                                        "video",
                                                        converter="deinterlace ! videoconvert",
                                                        mixed_srcs={
                                                             "synchronized": {"mixer_props": "sink_1::alpha=0.5 sink_1::xpos=50 sink_1::ypos=50",  # noqa
                                                                              "sources":
                                                                              ("videotestsrc pattern=snow timestamp-offset=3000000000 ! 'video/x-raw,format=AYUV,width=640,height=480,framerate=(fraction)30/1' !  timeoverlay",  # noqa
                                                                               "videotestsrc pattern=smpte ! 'video/x-raw,format=AYUV,width=800,height=600,framerate=(fraction)10/1' ! timeoverlay")},  # noqa
                                                             "bgra": ("videotestsrc ! video/x-raw, framerate=\(fraction\)10/1, width=100, height=100",  # noqa
                                                                      "videotestsrc ! video/x-raw, framerate=\(fraction\)5/1, width=320, height=240")
                                                        },
                                                        valid_scenarios=valid_mixing_scenarios))

    test_manager.add_generators(
        test_manager.GstValidateMixerTestsGenerator("audiomixer.simple", test_manager,
                                                    "audiomixer",
                                                    "audio",
                                                    converter="audioconvert ! audioresample",
                                                    mixed_srcs={"basic": {"mixer_props": "",
                                                                "sources": ("audiotestsrc wave=triangle",
                                                                            "audiotestsrc wave=ticks")}},
                                                    valid_scenarios=valid_mixing_scenarios))

    return True
