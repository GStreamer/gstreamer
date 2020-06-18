if gst_dep.type_name() == 'internal'
   if meson.version().version_compare('>= 0.51')
    gst_tester = gst_proj.get_variable('gst_tester', false)
    if gst_tester == false
        if get_option('tests').enabled()
            error('Tests are enabled for gst-plugins-base but were not enabled for the gstreamer subproject')
        endif
        subdir_done()
    endif
  else
    # Workaround for meson < 0.51. Remove once we bump the requirement.
    # If you hit this, disable tests in gst-plugins-base.
    gst_tester = gst_proj.get_variable('gst_tester')
  endif
else
    gst_tester = find_program('gst-tester-@0@'.format(api_version), required: get_option('tests'))
    if not gst_tester.found()
        subdir_done()
    endif
endif

tests = [
    'audiotestsrc/reverse',
    'videorate/10_to_1fps',
    'videorate/reverse.10_to_1fps',
    'videorate/reverse.10_to_30fps',
    'videorate/reverse.1_to_10fps',
    'videorate/reverse.30fps',
    'videorate/reverse.variable_to_10fps',
    'videorate/change_rate_while_playing',
    'videorate/change_rate_reverse_playback',
    'videorate/rate_0_5',
    'videorate/rate_0_5_with_decoder',
    'videorate/rate_2_0',
    'videorate/rate_2_0_with_decoder',
]

env = environment()
env.set('GST_PLUGIN_PATH_1_0', meson.build_root(), pluginsdirs)
env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), 'validate'))
env.set('GST_PLUGIN_SCANNER_1_0', gst_plugin_scanner_path)
env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer', 'gst-validate',
    'gst-plugins-base@' + meson.build_root())

foreach t: tests
    test_dir_name = t.split('/')
    test_name = 'validate'
    foreach c: test_dir_name
        test_name += '.' + c
    endforeach
    test_env = env
    test_env.set('GST_VALIDATE_LOGSDIR', join_paths(meson.current_build_dir(), test_name))
    test_file = join_paths(meson.current_source_dir(), t + '.validatetest')
    test(test_name, gst_tester, args: [test_file, '--use-fakesinks'],
        env: test_env, timeout : 3 * 60, protocol: 'tap')
endforeach
