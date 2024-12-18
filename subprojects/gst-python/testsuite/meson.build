runtests = files('runtests.py')

tests = [
    ['Test gst', 'test_gst.py'],
    ['Test fundamentals', 'test_types.py'],
    ['Test plugins', 'test_plugin.py'],
    ['Test analytics', 'test_analytics.py', ['gst-plugins-bad/gst-libs/gst/analytics', 'gst-plugins-base/gst-libs/gst/video']],
]

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

gst_deps_defs = [
  ['gstreamer', ['gstreamer', 'gst_dep'], 'libgst'],
  ['gstreamer-app', ['gst-plugins-base', 'app_dep'], 'gstapp'],
  ['gstreamer-audio', ['gst-plugins-base', 'audio_dep'], 'gstaudio'],
  ['gstreamer-base', ['gstreamer', 'gst_base_dep'], 'gst_base'],
  ['gstreamer-controller', ['gstreamer', 'gst_controller_dep'], 'gst_controller'],
  ['gstreamer-fft', ['gst-plugins-base', 'fft_dep'], 'gstfft'],
  ['gstreamer-net', ['gstreamer', 'gst_net_dep'], 'gst_net'],
  ['gstreamer-pbutils', ['gst-plugins-base', 'pbutils_dep'], 'pbutils'],
  ['gstreamer-riff', ['gst-plugins-base', 'riff_dep'], 'gstriff'],
  ['gstreamer-rtp', ['gst-plugins-base', 'rtp_dep'], 'gst_rtp'],
  ['gstreamer-rtsp', ['gst-plugins-base', 'rtsp_dep'], 'gst_rtsp'],
  ['gstreamer-sdp', ['gst-plugins-base', 'sdp_dep'], 'gstsdp'],
  ['gstreamer-tag', ['gst-plugins-base', 'tag_dep'], 'gsttag'],
  ['gstreamer-video', ['gst-plugins-base', 'video_dep'], 'gstvideo'],
  ['gstreamer-webrtc', ['gst-plugins-bad', 'gstwebrtc_dep'], 'gstwebrtc'],
  ['gstreamer-rtsp-server', ['gst-rtsp-server', 'gst_rtsp_server_dep'], 'gst_rtsp_server'],
]

api_version = '1.0'
gst_required_version = '>=@0@'.format(meson.project_version())
gst_deps = []

foreach dep: gst_deps_defs
  gst_deps += [dependency(dep.get(0) + '-' + api_version, version: gst_required_version,
    fallback: dep.get(1))]
endforeach

foreach i: tests
    test_name = i.get(0)
    env = environment()
    env.set('GST_OVERRIDE_SRC_PATH', join_paths (meson.current_source_dir(), '..', 'gi', 'overrides'))
    env.set('GST_OVERRIDE_BUILD_PATH', join_paths (meson.current_build_dir(), '..', 'gi', 'overrides'))
    env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer', 'gst-plugins-base',
        'gst-python@' + meson.project_build_root())
    env.set('GST_PLUGIN_PATH_1_0', meson.global_build_root(), pluginsdirs + pypluginsdir)
    env.set('GST_REGISTRY', join_paths(meson.current_build_dir(), '@0@.registry'.format(test_name)))

    l = 0
    testsenv_ld_library_path = []
    foreach dep: gst_deps
        if dep.type_name() == 'pkgconfig'
            testsenv_ld_library_path += [dep.get_variable('libdir')]
        else
            depdef = gst_deps_defs[l][1]
            libname = gst_deps_defs[l].get(2, '')
            if libname != ''
                proj = subproject(depdef[0])
                libpath = proj.get_variable(libname).full_path().split('/')
                dirname = ''
                j = 1
                foreach comp: libpath
                    if j < libpath.length()
                        dirname += '/' + comp
                    endif
                    j += 1
                endforeach
                testsenv_ld_library_path += [dirname]
            endif
        endif
        l += 1
    endforeach

    foreach j: i.get(2, [])
      path = meson.project_build_root() / '..' / j
      env.append('GI_TYPELIB_PATH', path)

      if build_machine.system() == 'windows'
	      env.append('PATH', path)
	      env.append('PATH', testsenv_ld_library_path)
      elif build_machine.system() == 'linux'
	      env.append('LD_LIBRARY_PATH', path)
	      env.append('LD_LIBRARY_PATH', testsenv_ld_library_path)
      else
	      env.append('DYLD_LIBRARY_PATH', path)
	      env.append('DYLD_LIBRARY_PATH', testsenv_ld_library_path)
      endif
    endforeach

    test(test_name, python, args: [runtests, i.get(1)], env: env)
endforeach
