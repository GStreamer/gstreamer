runtests = find_program('runtests.py')

tests = [
    ['Test gst', 'test_gst.py'],
    ['Test fractions', 'test_fraction.py']
]

pluginsdirs = []
if not meson.is_subproject()
  pkgconfig = find_program('pkg-config')
  runcmd = run_command(pkgconfig, '--variable=pluginsdir',
      'gstreamer-' + api_version)
  if runcmd.returncode() == 0
      pluginsdirs = runcmd.stdout().split()
  else
    error('Could not determine GStreamer core plugins directory for unit tests.')
  endif
endif

foreach i: tests
    test_name = i.get(0)
    env = environment()
    env.set('GST_PLUGIN_PATH_1_0', meson.build_root())
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    foreach plugindir: pluginsdirs
      env.append('GST_PLUGIN_PATH_1_0', plugindir)
    endforeach
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), i.get(1)))
    test(test_name, runtests, args: [i.get(1)], env: env)
endforeach
