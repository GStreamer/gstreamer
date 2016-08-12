mve_sources = [
  'gstmve.c',
  'gstmvemux.c',
  'gstmvedemux.c',
  'mveaudiodec.c',
  'mvevideodec8.c',
  'mvevideodec16.c',
  'mveaudioenc.c',
  'mvevideoenc8.c',
  'mvevideoenc16.c',
]

gstmve = library('gstmve',
  mve_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
