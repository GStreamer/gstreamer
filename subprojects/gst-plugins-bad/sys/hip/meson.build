hip_sources = [
  'gsthip-enums.cpp',
  'gsthipbasefilter.cpp',
  'gsthipbufferpool.cpp',
  'gsthipconverter.cpp',
  'gsthipconvertscale.cpp',
  'gsthipdevice.cpp',
  'gsthiploader.cpp',
  'gsthipmemory.cpp',
  'gsthipmemorycopy.cpp',
  'gsthiprtc.cpp',
  'gsthiputils.cpp',
  'plugin.cpp',
]

doc_sources = []
foreach s: hip_sources
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'hip': pathsep.join(doc_sources)
}

extra_args = [
  '-DGST_USE_UNSTABLE_API',
]

hip_option = get_option('hip')
if hip_option.disabled()
  subdir_done()
endif

if host_system not in ['linux', 'windows']
  subdir_done()
endif

hip_incdir = include_directories('./stub')

gsthip = library('gsthip', hip_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args: gst_plugins_bad_args + extra_args,
  include_directories : [configinc, hip_incdir],
  dependencies : [gstbase_dep, gstvideo_dep, gmodule_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsthip]
