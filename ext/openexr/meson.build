openexr_dep = dependency('OpenEXR', required: get_option('openexr'))
if openexr_dep.found()
  openexr_extra_cpp_args = cxx.get_supported_arguments(['-std=c++98'])
  gstopenexr = library('gstopenexr',
    'gstopenexr.c',
    'gstopenexrdec.cpp',
    c_args: gst_plugins_bad_args,
    cpp_args: gst_plugins_bad_args + openexr_extra_cpp_args,
    link_args: noseh_link_args,
    include_directories: [configinc, libsinc],
    dependencies: [gstvideo_dep, openexr_dep],
    install: true,
    install_dir: plugins_install_dir,
  )
  pkgconfig.generate(gstopenexr, install_dir: plugins_pkgconfig_install_dir)
endif
