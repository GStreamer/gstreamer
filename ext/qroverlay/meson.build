qrencode_dep = dependency('libqrencode', required: get_option('qroverlay'))
json_dep = dependency('json-glib-1.0', fallback : ['json-glib', 'json_glib_dep'])
if qrencode_dep.found()
  gstqroverlay = library('gstqroverlay', ['gstqroverlay.c'],
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, qrencode_dep, json_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstqroverlay, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstqroverlay]
endif

