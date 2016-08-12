gstwawparse = library('gstwavparse',
  'gstwavparse.c',
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstpbutils_dep, gstriff_dep, gstaudio_dep,
		  gsttag_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
