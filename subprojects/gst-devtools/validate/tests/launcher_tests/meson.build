if launcher.found()
    test_name = 'validate/launcher_tests'

    env = environment()
    env.set('GST_STATE_IGNORE_ELEMENTS', '')
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()] + pluginsdirs)
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    env.set('GST_PLUGIN_SCANNER_1_0', gst_plugin_scanner_path)

    test(test_name, launcher, args: ['-o', meson.project_build_root() + '/validate-launcher-output/',
      meson.current_source_dir() + '/test_validate.py', '--validate-tools-path',
      join_paths(meson.current_build_dir(), '..', '..', 'tools')],
      env: env)
endif
