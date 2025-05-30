taglib_dep = dependency('taglib', version : '>= 1.5', required : get_option('taglib'))

taglib_sources = [
  'gstapev2mux.cc',
  'gstid3v2mux.cc',
  'gsttaglibelement.c',
  'gsttaglibplugin.c',
]

taglib_headers = [
  'gsttaglibelements.h',
  'gstapev2mux.h',
  'gstid3v2mux.h',
]

doc_sources = []
foreach s: taglib_sources + taglib_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'taglib': pathsep.join(doc_sources)
}

if taglib_dep.found() and add_languages('cpp', native: false, required: get_option('taglib'))
  extra_args = []
  cxx = meson.get_compiler('cpp')
  if cxx.has_argument('-fvisibility=hidden')
    extra_args += ['-fvisibility=hidden']
  endif
  if cxx.has_argument('-fno-strict-aliasing')
    extra_args += ['-fno-strict-aliasing']
  endif

  gsttaglib = library('gsttaglib',
    taglib_sources,
    c_args : gst_plugins_good_args + ['-DGST_USE_UNSTABLE_API'],
    cpp_args : gst_plugins_good_args + ['-DGST_USE_UNSTABLE_API'] + extra_args,
    include_directories : [configinc, libsinc],
    dependencies : [gsttag_dep, taglib_dep],
    install : true,
    install_dir : plugins_install_dir,
    override_options : ['cpp_std=c++17'],
  )
  plugins += [gsttaglib]
endif
