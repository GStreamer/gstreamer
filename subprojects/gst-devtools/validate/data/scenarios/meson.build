_scenarios = ['simple_seeks.scenario',
              'seek_forward.scenario',
              'seek_backward.scenario',
              'seek_forward_backward.scenario',
              'reverse_playback.scenario',
              'fast_forward.scenario',
              'fast_backward.scenario',
              'alternate_fast_backward_forward.scenario',
              'pause_resume.scenario',
              'scrub_forward_seeking.scenario',
              'scrub_backward_seeking.scenario',
              'scrub_forward_seeking_full.scenario',
              'scrub_backward_seeking_full.scenario',
              'adaptive_video_size.scenario',
              'adaptive_video_framerate.scenario',
              'adaptive_video_framerate_size.scenario',
              'force_key_unit.scenario',
              'seek_with_stop.scenario',
              'switch_audio_track_while_paused.scenario',
              'switch_subtitle_track.scenario',
              'switch_subtitle_track_while_paused.scenario',
              'disable_subtitle_track_while_paused.scenario',
              'play_15s.scenario',
              'play_5s.scenario',
              'change_state_intensive.scenario',
              'switch_audio_track.scenario',
              'force_rtsp2.scenario',]

install_data(sources: _scenarios,
             install_dir: get_option('datadir') + '/gstreamer-' +
             apiversion + '/validate/scenarios')

install_subdir('includes',
             install_dir: get_option('datadir') + '/gstreamer-' +
             apiversion + '/validate/scenarios')

install_subdir('rtsp_overrides',
             install_dir: get_option('datadir') + '/gstreamer-' +
             apiversion + '/validate/scenarios')

env = environment()
env.prepend('GST_VALIDATE_SCENARIOS_PATH', meson.current_source_dir())
meson.add_devenv(env)
