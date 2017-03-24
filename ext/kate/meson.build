kate_sources = [
  'gstkate.c',
  'gstkatedec.c',
  'gstkateenc.c',
  'gstkateparse.c',
  'gstkatetag.c',
  'gstkateutil.c',
  'gstkatespu.c',
]

kate_dep = dependency('kate', version : '>=0.1.7', required : false)
# Not used
#tiger_dep = dependency('tiger', version : '>=0.3.2', required : false)

if kate_dep.found()
  gstkate = library('gstkate',
    kate_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gstvideo_dep, gsttag_dep, kate_dep],
    install : true,
    install_dir : plugins_install_dir)
endif
