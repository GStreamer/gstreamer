magicleap_sources = [
  'plugin.c',
  'mlaudiosink.c',
  'mlaudiowrapper.cpp',
]

magicleap_headers = [
  'mlaudiosink.h',
  'mlaudiowrapper.h',
]

doc_sources = []
foreach s: magicleap_sources + magicleap_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'magicleap': pathsep.join(doc_sources)
}

libmlaudio_dep = cc.find_library('ml_audio', required : get_option('magicleap'))
liblumin_core_dep = cc.find_library('lumin_rt_core_1_7', required : get_option('magicleap'))
liblumin_app_dep = cc.find_library('lumin_rt_app_1_7', required : get_option('magicleap'))

if libmlaudio_dep.found()
  gstmagicleap = library('gstmagicleap',
    magicleap_sources,
    c_args : gst_plugins_bad_args,
    cpp_args : gst_plugins_bad_args + [
      '-fno-exceptions',
      '-fno-rtti',
    ],
    include_directories : [configinc, libsinc],
    dependencies : [gst_dep, gstbase_dep, gstaudio_dep, libmlaudio_dep,
                    liblumin_core_dep, liblumin_app_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += gstmagicleap
endif
