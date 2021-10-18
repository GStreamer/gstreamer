tests = [
  [ 'elements/vaapipostproc' ],
]

if USE_DRM
  tests += [
  [ 'elements/vaapioverlay' ]
]
endif

test_deps = [gst_dep, gstbase_dep, gstvideo_dep, gstcheck_dep]
test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_USE_UNSTABLE_API',
]

pluginsdirs = []
if gst_dep.type_name() == 'pkgconfig'
  pluginsdirs = [gst_dep.get_variable('pluginsdir')]
endif

foreach t : tests
  fname = '@0@.c'.format(t.get(0))
  test_name = t.get(0).underscorify()
  extra_sources = [ ]
  extra_deps = [ ]
  env = environment()
  env.set('CK_DEFAULT_TIMEOUT', '20')
  env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
  env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()] + pluginsdirs)
  env.set('GST_REGISTRY', join_paths(meson.current_build_dir(), '@0@.registry'.format(test_name)))
  exe = executable(test_name, fname, extra_sources,
    include_directories : [configinc, libsinc],
    c_args : ['-DHAVE_CONFIG_H=1' ] + test_defines,
    dependencies : test_deps + extra_deps,
  )
  test(test_name, exe, env: env, timeout: 3 * 60)
endforeach
