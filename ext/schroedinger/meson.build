schro_sources = [
  'gstschro.c',
  'gstschrodec.c',
  'gstschroenc.c',
  'gstschroutils.c',
]

schro_dep = dependency('schroedinger-1.0', version : '>= 1.0.10', required : false)

if schro_dep.found()
  gstschro = library('gstschro',
    schro_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, schro_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
