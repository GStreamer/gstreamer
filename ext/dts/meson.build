dca_dep = dependency('libdca', required : false)

if not dca_dep.found() and cc.has_header_symbol('dca.h', 'dca_init')
  dca_dep = cc.find_library('dca', required : false)
endif

if dca_dep.found()
  gstdtsdec = library('gstdtsdec',
    'gstdtsdec.c',
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, orc_dep, dca_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
