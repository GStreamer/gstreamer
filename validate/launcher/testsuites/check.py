# -*- Mode: Python -*- vi:si:et:sw=4:sts=4:ts=4:syntax=python
#
# Copyright (c) 2016,Thibault Saunier <thibault.saunier@osg.samsung.com>
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
GStreamer unit tests
"""
from launcher import utils

TEST_MANAGER = "check"

KNOWN_NOT_LEAKY = r'^check.gst-devtools.*|^check.gstreamer.*|^check-gst-plugins-base|^check.gst-plugins-ugly|^check.gst-plugins-good'

# These tests take very long compared to what they add, so let's skip them.
LONG_VALGRIND_TESTS = [
    (r'check.[a-z-]*.generic_states.test_state_changes_down_seq', 'enough to run one of the sequences'),
    (r'check.[a-z-]*.generic_states.test_state_changes_up_seq', 'enough to run one of the sequences',),
    (r'check.gstreamer.gst_gstelement.test_foreach_pad$', '48s'),
    (r'check.gstreamer.gst_gstinfo.info_post_gst_init_category_registration$', '21s'),
    (r'check.gstreamer.gst_gstsystemclock.test_resolution$', '60s'),
    (r'check.gstreamer.libs_aggregator.test_infinite_seek$', '20s'),
    (r'check.gstreamer.libs_aggregator.test_infinite_seek_50_src$', '20s'),
    (r'check.gstreamer.libs_gstharness.test_harness_element_ref$', '20s'),
    (r'check.gstreamer.pipelines_simple_launch_lines.test_2_elements$', '58s'),
    (r'check.gstreamer.pipelines_stress.test_stress$', '54s'),
    (r'check.gstreamer.pipelines_stress.test_stress_preroll$', '27s'),
    (r'check.gst-plugins-base.elements_appsrc.test_appsrc_block_deadlock$', '265.595s'),
    (r'check.gst-plugins-base.elements_audioresample.test_fft$', '91.247s'),
    (r'check.gst-plugins-base.elements_audioresample.test_timestamp_drift$', '141.784s'),
    (r'check.gst-plugins-base.elements-videoscale.*', 'superlong'),
    (r'check.gst-plugins-base.libs_video.test_overlay_blend$', '74.096s'),
    (r'check.gst-plugins-base.libs_video.test_video_color_convert$', '345.271s'),
    (r'check.gst-plugins-base.libs_video.test_video_formats$', '70.987s'),
    (r'check.gst-plugins-base.libs_video.test_video_size_convert$', '56.387s'),
    (r'check.gst-plugins-base.elements_audiointerleave.test_audiointerleave_2ch_pipeline_$', '5 *51.069s'),
    (r'check.gst-plugins-base.elements_multifdsink.test_client_kick$', '46.909s'),
    (r'check.gst-plugins-base.elements_videotestsrc.test_all_patterns$', '?'),
    (r'check.gst-plugins-base.elements_videotestsrc.test_patterns_are_deterministic$', '?'),
    (r'check.gst-plugins-good.elements_shapewipe.test_general$', '325s'),
    (r'check.gst-plugins-good.elements_videocrop.test_cropping$', '245s'),
    (r'check.gst-plugins-good.elements_videomixer.*', '30s (alsodeprecated)'),
    (r'check.gst-plugins-good.elements_rtp_payloading.rtp_jpeg_packet_loss$', '109s'),
    (r'check.gst-plugins-good.elements_videomixer.test_play_twice_then_add_and_play_again$', '55s'),
    (r'check.gst-plugins-good.pipelines_effectv.test_quarktv$', '53s'),
    (r'check.gst-plugins-good.elements_deinterlace.test_mode_disabled_passthrough$', '52s'),
    (r'check.gst-plugins-good.elements_deinterlace.test_mode_auto_deinterlaced_passthrough$', '28s'),
    (r'check.gst-plugins-good.elements_deinterleave.test_2_channels_caps_change$', '30s'),
    (r'check.gst-plugins-good.elements_deinterleave.test_2_channels$', '22s'),
    (r'check.gst-plugins-good.elements_rtpjitterbuffer.test_fill_queue$', '22s'),
    (r'check.gst-plugins-good.elements_splitmux.test_splitmuxsink_async$', '20s'),
    (r'check.gst-plugins-good.elements_videomixer.test_play_twice$', '22s'),
    (r'check.gst-editing-services.nle_simple.test_one_after_other$', '40s'),
    (r'check.gst-editing-services.nle_nleoperation.test_pyramid_operations_expandable', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/67'),
    (r'check.gst-editing-services.nle_nleoperation.test_complex_operations', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/67'),
    (r'check.gst-editing-services.nle_nleoperation.test_pyramid_operations', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/67'),
    (r'check.gst-editing-services.nle_nleoperation.test_pyramid_operations2', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/67'),
    (r'check.gst-editing-services.nle_complex.test_one_above_another', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/67'),
    (r'check.gst-editing-services.nle_nleoperation.test_simple_operation', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/67'),
    (r'check.gst-editing-services.seek_with_stop.check_clock_sync', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/67'),
    (r'check.gst-editing-services.edit_while_seeked_with_stop', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/67'),
    (r'check.gst-editing-services.seek_with_stop', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/67'),
    (r'check.gst-editing-services.nle_tempochange.test_tempochange_seek', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/67'),
    (r'check.gst-editing-services.nle_tempochange.test_tempochange_play', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/67'),
]

VALGRIND_BLACKLIST = [
    (r'check.gstreamer.gst_gstsystemclock.test_stress_cleanup_unschedule', '?'),
    (r'check.gstreamer.gst_gstsystemclock.test_stress_reschedule', '?'),
    (r'check.gstreamer.tools_gstinspect', '?'),
    (r'check.gst-plugins-base.elements_videoscale', '?'),
    (r'check.gst-plugins-base.pipelines_gl_launch_lines', '?'),
    (r'check.gst-plugins-base.libs_gstgl', 'driver leaks / memory access'),
    (r'check.gst-plugins-base.elements_gl', 'driver leaks / memory access'),
    (r'check.gst-plugins-base.elements_libvisual', 'uninitialized memory access'),
    (r'check.gst-plugins-base.generic_states', 'need to add gl elements to ignore list but only if using valgrind'),
    (r'check.gst-plugins-good.elements_rtpjitterbuffer.test_push_backward_seq', 'flaky in valgrind'),
    (r'check.gst-plugins-good.elements_rtpjitterbuffer.test_push_unordered', 'flaky in valgrind'),
    (r'check.gst-plugins-bad.elements_assrender', '?'),
    (r'check.gst-plugins-bad.elements_camerabin', '?'),
    (r'check.gst-plugins-bad.elements_line21', '?'),
    (r'check.gst-plugins-bad.elements_mpeg2enc', '?'),
    (r'check.gst-plugins-bad.elements_mplex', '?'),
    (r'check.gst-plugins-bad.elements_mxfmux', '?'),
    (r'check.gst-plugins-bad.elements_x265enc', '?'),
    (r'check.gst-plugins-bad.elements_zbar', '?'),
    (r'check.gst-plugins-bad.elements_webrtcbin.test_data_channel_remote_notify', 'Need to fix leaks'),
    (r'check.gst-plugins-bad.elements_webrtcbin.test_data_channel_pre_negotiated', 'Need to fix leaks'),
    (r'check.gst-plugins-bad.elements_webrtcbin.test_data_channel_low_threshold', 'Need to fix leaks'),
    (r'check.gst-plugins-bad.elements_webrtcbin.test_data_channel_transfer_data', 'Need to fix leaks'),
    (r'check.gst-plugins-bad.elements_webrtcbin.test_data_channel_transfer_string', 'Need to fix leaks'),
    (r'check.gst-plugins-bad.elements_webrtcbin.test_data_channel_create_after_negotiate', 'Need to fix leaks'),
    (r'check.gst-plugins-bad.elements_webrtcbin.test_data_channel_max_message_size', 'Need to fix leaks'),
    (r'check.gst-plugins-bad.elements_webrtcbin.test_renego_data_channel_add_stream', 'Need to fix leaks (possibly flaky)'),
    (r'check.gst-libav.generic_plugin_test', '?'),
    (r'check.gst-libav.generic_libavcodec_locking', '?'),
    (r'check.gst-libav.elements_avdemux_ape', '?'),
    (r'check.gst-editing-services.pythontests', 'Need to figure out how to introduce python suppressions'),
    (r'check.gst-editing-services.check_keyframes_in_compositor_two_sources', 'Valgrind exit with an exitcode 20 but shows no issue: https://gitlab.freedesktop.org/thiblahute/gst-editing-services/-/jobs/4079972')
]

BLACKLIST = [
    (r'check.gstreamer.gst_gstsystemclock.test_stress_cleanup_unschedule', 'flaky under high server load'),
    (r'check.gstreamer.gst_gstsystemclock.test_stress_reschedule', 'flaky under high server load'),
    (r'check.gstreamer.pipelines_seek.test_loopback_2$', '?'),
    (r'check.gstreamer.gst_gstelement.test_foreach_pad$', '?'),
    (r'check.gstreamer.libs_baseparse.parser_pull_short_read$', '?'),
    (r'check.gst-plugins-base.elements_multisocketsink.test_sending_buffers_with_9_gstmemories$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/525'),
    (r'check.gst-plugins-base.elements_multisocketsink.test_client_next_keyframe$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/516'),
    (r'check.gst-plugins-base.elements_multisocketsink.test_add_client$', ''),
    (r'check.gst-plugins-base.libs_gstglcolorconvert.test_reorder_buffer$', '?'),
    (r'check.gst-plugins-base.elements_audiotestsrc.test_layout$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/535'),
    (r'check.gst-plugins-good.elements_souphttpsrc.test_icy_stream$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/307'),
    (r'check.gst-plugins-good.elements_rtpbin.test_sender_eos$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/527'),
    (r'check.gst-plugins-good.elements_rtpbin.test_cleanup_recv$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/546'),
    (r'check.gst-plugins-good.elements_flvmux.test_incrementing_timestamps$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/530'),
    (r'check.gst-plugins-good.elements_flvmux.test_video_caps_late$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/543'),
    (r'check.gst-plugins-base.elements_appsrc.test_appsrc_blocked_on_caps$', '?'),
    (r'check.gst-plugins-good.elements_splitmux.test_splitmuxsink$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/626'),
    (r'check.gst-plugins-good.elements_splitmux.test_splitmuxsrc_sparse_streams$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/544'),
    (r'check.gst-plugins-good.elements_splitmux.test_splitmuxsrc_caps_change$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/547'),
    (r'check.gst-plugins-bad.elements_dtls.test_data_transfer$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/811'),
    (r'check.gst-plugins-bad.elements_dtls.test_create_and_unref$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/811'),
    (r'check.gst-plugins-bad.elements_camerabin.test_image_video_cycle$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/864'),
    (r'check.gst-plugins-bad.elements_camerabin.test_single_video_recording$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/864#note_101558'),
    (r'check.gst-plugins-bad.elements_camerabin.test_multiple_video_recordings$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/864#note_101646'),
    (r'check.gst-plugins-bad.elements_curlhttpsrc.test_multiple_http_requests$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/932'),
    (r'check.gst-plugins-good.elements_rtpsession.test_multiple_senders_roundrobin_rbs$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/548'),
    (r'check.gst-plugins-bad.elements_shm.test_shm_live$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/847'),
    (r'check.gst-plugins-good.elements_splitmux.test_splitmuxsink_async$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-good/issues/531'),
    (r'check.gst-plugins-bad.elements_netsim.netsim_stress$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/849'),
    (r'check.gst-editing-services.nle_complex.test_one_expandable_another$', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/55'),
    (r'check.gst-editing-services.nle_simple.test_simplest$', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/57'),
    (r'check.gst-editing-services.ges_basic.test_ges_pipeline_change_state$', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/58'),
    (r'check.gst-editing-services.pythontests.pyunittest.python.test_timeline.TestTransitions.test_transition_type$', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/62'),
    (r'check.gst-editing-services.pythontests.pyunittest.python.test_timeline.TestTransitions.test_auto_transition$', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/63'),
    (r'check.gst-plugins-base.pipelines_tcp.test_that_tcpserversink_and_tcpclientsrc_are_symmetrical$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/221'),
    (r'check.gstreamer.elements_capsfilter.test_unfixed_downstream_caps$', 'https://gitlab.freedesktop.org/gstreamer/gstreamer/issues/335'),
    (r'check.gst-rtsp-server.gst_rtspclientsink.test_record$', 'https://gitlab.freedesktop.org/gstreamer/gst-rtsp-server/issues/55'),
    (r'check.gst-rtsp-server.gst_rtspserver.test_shared_udp$', 'https://gitlab.freedesktop.org/gstreamer/gst-rtsp-server/issues/61'),
    (r'check.gst-plugins-base.elements_audiomixer.test_flush_start_flush_stop$', 'https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/555'),
    (r'check.gstreamer-sharp.SdpTests$', 'https://gitlab.freedesktop.org/gstreamer/gstreamer-sharp/issues/17'),
    (r'check.gst-devtools.validate.launcher_tests.test_validate.launch_pipeline.not_negotiated.caps_query_failure.play_15s$', '?'),
    (r'check.gst-editing-services.nle_simple.test_one_bin_after_other$', 'https://gitlab.freedesktop.org/gstreamer/gst-editing-services/issues/66'),
    (r'check.gstreamer-vaapi.*$', 'only run the tests explicitly'),
]


def setup_tests(test_manager, options):
    if options.gst_check_leak_trace_testnames == 'known-not-leaky':
        options.gst_check_leak_trace_testnames = KNOWN_NOT_LEAKY
    test_manager.set_default_blacklist(BLACKLIST)
    if options.valgrind:
        test_manager.set_default_blacklist(VALGRIND_BLACKLIST)
        if options.long_limit <= utils.LONG_TEST:
            test_manager.set_default_blacklist(LONG_VALGRIND_TESTS)

    test_manager.register_tests()
    return True
