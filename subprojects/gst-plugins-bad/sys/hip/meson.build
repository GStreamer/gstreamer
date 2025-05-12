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

extra_deps = []

hip_option = get_option('hip')
if hip_option.disabled()
  subdir_done()
endif

if host_system not in ['linux', 'windows']
  subdir_done()
endif

hip_incdir = include_directories('./stub')
hip_cdata = configuration_data()
if gstcuda_dep.found()
  hip_cdata.set('HAVE_GST_CUDA', true)
  extra_deps += [gstcuda_dep]
endif

configure_file(
  output: 'gsthip-config.h',
  configuration: hip_cdata,
)

gsthip = library('gsthip', hip_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args: gst_plugins_bad_args + extra_args,
  include_directories : [configinc, hip_incdir],
  dependencies : [gstbase_dep, gstvideo_dep, gmodule_dep] + extra_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsthip]
