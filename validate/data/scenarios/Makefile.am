scenariosdir=${datadir}/gstreamer-$(GST_API_VERSION)/validate/scenarios
scenarios_DATA = simple_seeks.scenario \
                 seek_forward.scenario \
                 seek_backward.scenario \
                 seek_forward_backward.scenario \
                 reverse_playback.scenario \
                 fast_forward.scenario \
                 fast_backward.scenario \
                 alternate_fast_backward_forward.scenario \
                 pause_resume.scenario \
                 scrub_forward_seeking.scenario \
                 scrub_backward_seeking.scenario \
                 scrub_forward_seeking_full.scenario \
                 scrub_backward_seeking_full.scenario \
                 adaptive_video_size.scenario \
                 adaptive_video_framerate.scenario \
                 adaptive_video_framerate_size.scenario\
                 force_key_unit.scenario\
                 seek_with_stop.scenario\
                 switch_audio_track_while_paused.scenario\
                 switch_subtitle_track.scenario\
                 switch_subtitle_track_while_paused.scenario\
                 disable_subtitle_track_while_paused.scenario\
                 change_state_intensive.scenario\
                 play_15s.scenario \
                 switch_audio_track.scenario \
                 trick_mode_seeks.scenario

scenariosincsdir=${datadir}/gstreamer-$(GST_API_VERSION)/validate/scenarios/includes
scenariosincs_DATA = includes/default-seek-flags.scenario

rtsp_overrides_scenariosincsdir=${datadir}/gstreamer-$(GST_API_VERSION)/validate/scenarios/rtsp_overrides/includes
rtsp_overrides_scenariosincs_DATA = rtsp_overrides/includes/default-seek-flags.scenario

EXTRA_DIST = ${scenarios_DATA} ${scenariosincs_DATA} ${rtsp_overrides_scenariosincs_DATA}
