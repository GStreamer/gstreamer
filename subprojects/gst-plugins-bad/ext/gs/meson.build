gs_sources = [
  'gstgscommon.cpp',
  'gstgssink.cpp',
  'gstgssrc.cpp',
  'gstgs.cpp',
]

gs_headers = [
  'gstgscommon.h',
  'gstgssink.h',
  'gstgssrc.h',
]

doc_sources = []
foreach s: gs_sources + gs_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'gs': pathsep.join(doc_sources)
}

gs_dep = dependency('google_cloud_cpp_storage', version : '>= 1.25.0', required : get_option('gs'))

if gs_dep.found()
  gstgs = library('gstgs',
    gs_sources,
    c_args : gst_plugins_bad_args,
    cpp_args : gst_plugins_bad_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gs_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstgs]
endif
