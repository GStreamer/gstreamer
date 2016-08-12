faac_dep = cc.find_library('faac', required : false)

if faac_dep.found() and cc.has_header_symbol('faac.h', 'faacEncOpen')
  gstfaac = library('gstfaac',
    'gstfaac.c',
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gstpbutils_dep, gsttag_dep, faac_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
