wavpack_sources = [
  'gstwavpack.c',
  'gstwavpackcommon.c',
  'gstwavpackdec.c',
  'gstwavpackenc.c',
  'gstwavpackstreamreader.c',
]

wavpack_dep = dependency('wavpack', version : '>= 4.60.0', required : false)

if wavpack_dep.found()
  gstwavpack = library('gstwavpack',
    wavpack_sources,
    c_args : gst_plugins_good_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gstaudio_dep, wavpack_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
