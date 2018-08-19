neon_dep = dependency('neon', version: ['>= 0.27', '<= 0.30.99'],
                      required : get_option('neon'))

if neon_dep.found()
  gstneon = library('gstneonhttpsrc',
    'gstneonhttpsrc.c',
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, neon_dep],
    install : true,
    install_dir : plugins_install_dir)
  pkgconfig.generate(gstneon, install_dir : plugins_pkgconfig_install_dir)
endif
