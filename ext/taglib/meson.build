taglib_sources = [
  'gstapev2mux.cc',
  'gstid3v2mux.cc',
  'gsttaglibplugin.c',
]

taglib_dep = dependency('taglib', version : '>= 1.5', required : false)

if taglib_dep.found() and add_languages('cpp')
  gsttaglib = library('gsttaglib',
    taglib_sources,
    c_args : gst_plugins_good_args + ['-DGST_USE_UNSTABLE_API'],
    include_directories : [configinc, libsinc],
    dependencies : [gsttag_dep, taglib_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
