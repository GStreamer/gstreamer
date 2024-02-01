subdir ('scenarios')

install_data (['geslaunch.py'],
    install_dir : join_paths(get_option('libdir'),
      'gst-validate-launcher', 'python', 'launcher', 'apps'))

env = environment()
env.prepend('GST_VALIDATE_APPS_DIR', meson.current_source_dir())
meson.add_devenv(env)

gst_tester = find_program('gst-tester-@0@'.format(apiversion), required: get_option('tests'))
if not gst_tester.found()
    subdir_done()
endif

tests = [
    'nle/simple_source_playback',
    'nle/simple_source_in_composition_playback',
    'nle/ensure_no_unnecessary_stack_init_seeks',
]

env = environment()
env.set('GST_PLUGIN_PATH_1_0', meson.global_build_root(), pluginsdirs)
env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), 'validate'))
env.set('GST_PLUGIN_SCANNER_1_0', gst_plugin_scanner_path)
env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer', 'gst-validate',
    'gst-plugins-base@' + meson.project_build_root(),
    'gst-editing-services@' + meson.project_build_root(),
)

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
