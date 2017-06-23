# Very much not going to implement all kinds of logic around aalib-config
# or cater for non-standard prefixes.
if cc.has_header('aalib.h')
  libaa_dep = cc.find_library('aa', required : false)
  if libaa_dep.found()
    library('gstaasink', 'gstaasink.c',
      c_args : gst_plugins_good_args,
      link_args : noseh_link_args,
      include_directories : [configinc],
      dependencies : [gstvideo_dep, gstbase_dep, libaa_dep],
      install : true,
      install_dir : plugins_install_dir)
  endif
endif
