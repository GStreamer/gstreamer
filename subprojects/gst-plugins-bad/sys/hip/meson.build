hip_sources = [
  'gsthipbasefilter.cpp',
  'gsthipcompositor.cpp',
  'gsthipconvertscale.cpp',
  'gsthipmemorycopy.cpp',
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

if not gsthip_dep.found()
  subdir_done()
endif

hip_cdata = configuration_data()

if gstcuda_dep.found()
  hip_cdata.set('HAVE_GST_CUDA', true)
  extra_deps += [gstcuda_dep]
endif

if gsthip_gl_dep.found()
  hip_cdata.set('HAVE_GST_HIP_GL', true)
  extra_deps += [gsthip_gl_dep]
endif

configure_file(
  output: 'gsthip-config.h',
  configuration: hip_cdata,
)

gsthip = library('gsthip', hip_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args: gst_plugins_bad_args + extra_args,
  include_directories : [configinc, hipstub_incdir],
  dependencies : [gstbase_dep, gstvideo_dep, gmodule_dep, gsthip_dep, gstallocators_dep] + extra_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsthip]
