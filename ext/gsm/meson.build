gsm_dep = cc.find_library('gsm', required : false)

if gsm_dep.found() and (cc.has_header_symbol('gsm.h', 'gsm_create') or
			cc.has_header_symbol('gsm/smg.h', 'gsm_create'))
  gstgsm = library('gstgsm',
    ['gstgsm.c', 'gstgsmenc.c', 'gstgsmdec.c'],
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, gsm_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
