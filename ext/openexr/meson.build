openexr_dep = dependency('OpenEXR', required: get_option('openexr'))
if openexr_dep.found()
  openexr_cppargs = []

  if openexr_dep.version().version_compare('< 2.4.0')
    openexr_cppargs += cxx.get_supported_arguments(['-std=c++98'])
  endif

  gstopenexr = library('gstopenexr',
    'gstopenexr.c',
    'gstopenexrdec.cpp',
    c_args: gst_plugins_bad_args,
    cpp_args: gst_plugins_bad_args + openexr_cppargs,
    link_args: noseh_link_args,
    include_directories: [configinc, libsinc],
    dependencies: [gstvideo_dep, openexr_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  pkgconfig.generate(gstopenexr, install_dir: plugins_pkgconfig_install_dir)
  plugins += [gstopenexr]
endif
