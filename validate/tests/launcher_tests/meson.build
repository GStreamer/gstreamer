env = environment()
env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
env.set('GST_PLUGIN_PATH_1_0', meson.build_root())
if not meson.is_subproject()
  env.append('GST_PLUGIN_PATH_1_0', gst_dep.get_pkgconfig_variable('pluginsdir'))

  gst_plugins_base_dep = dependency('gstreamer-plugins-base-1.0')
  env.append('GST_PLUGIN_PATH_1_0', gst_plugins_base_dep.get_pkgconfig_variable('pluginsdir'))
endif

if launcher.found()
    test_name = 'validate/launcher_tests'
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))

    test(test_name, launcher, args: ['-o', meson.build_root() + '/validate-launcher-output/',
      meson.current_source_dir() + '/test_validate.py', '--validate-tools-path',
      join_paths(meson.current_build_dir(), '..', '..', 'tools')],
      env: env)
endif
