runtests = files('runtests.py')

tests = [
    ['Test gst', 'test_gst.py'],
    ['Test fundamentals', 'test_types.py'],
    ['Test plugins', 'test_plugin.py'],
]

pluginsdirs = []
if not meson.is_subproject()
  pkgconfig = find_program('pkg-config')
  runcmd = run_command(pkgconfig, '--variable=pluginsdir',
      'gstreamer-' + api_version, check: true)
  pluginsdirs = runcmd.stdout().split()
endif

runcmd = run_command(python, '-c', '''with open("@0@/mesonconfig.py", "w") as f:
    f.write("path='@1@'")'''.format(
  join_paths(meson.current_build_dir()), join_paths(meson.current_build_dir(), '..')),
  check: true)

pluginsdirs = []
if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + api_version, required : false)
  pluginsdirs = [gst_dep.get_variable('pluginsdir'),
                 pbase.get_variable('pluginsdir')]
endif

pypluginsdir = [join_paths (meson.project_build_root(), 'plugin'), meson.current_source_dir()]

foreach i: tests
    test_name = i.get(0)
    env = environment()
    env.set('GST_OVERRIDE_SRC_PATH', join_paths (meson.current_source_dir(), '..', 'gi', 'overrides'))
    env.set('GST_OVERRIDE_BUILD_PATH', join_paths (meson.current_build_dir(), '..', 'gi', 'overrides'))
    env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer', 'gst-plugins-base',
        'gst-python@' + meson.project_build_root())
    env.set('GST_PLUGIN_PATH_1_0', meson.global_build_root(), pluginsdirs + pypluginsdir)
    env.set('GST_REGISTRY', join_paths(meson.current_build_dir(), '@0@.registry'.format(test_name)))
    test(test_name, python, args: [runtests, i.get(1)], env: env)
endforeach
