launcher = find_program(meson.build_root() + '/validate/tools/gst-validate-launcher',
    required : false)

runcmd = run_command(getpluginsdir, 'gstreamer', 'gstreamer-' + apiversion,
    'gst-plugins-base', 'gst-plugins-base-' + apiversion)
if runcmd.returncode() == 0
    needed_plugins_dirs = runcmd.stdout().strip()
    message('Using GStreamer plug-ins in ' + needed_plugins_dirs)
else
    error('Could not determine GStreamer plugins directory for unit tests.')
endif

test_env = [
    'GST_PLUGIN_SYSTEM_PATH_1_0=',
    'GST_PLUGIN_PATH_1_0=' + needed_plugins_dirs,
    'GST_PLUGIN_SCANNER_1_0='+ meson.build_root() + '/libs/gst/helpers/gst-plugin-scanner',
]

if launcher.found()
    test_name = 'launcher_tests'
    test(test_name, launcher, args: ['-o', meson.build_root() + '/validate-launcher-output/',
      meson.current_source_dir() + '/test_validate.py', '--validate-tools-path',
      meson.build_root() + '/validate/tools/'],
      env: ['GST_REGISTRY=@0@/@1@.registry'.format(meson.current_build_dir(), test_name)] +
      test_env)
endif
