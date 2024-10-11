lcevcdecoder_sources = [
  'plugin.c',
  'gstlcevcdecutils.c',
  'gstlcevcdec.c',
  'gstlcevcdecodebin.c',
  'gstlcevch264decodebin.c',
]

lcevc_dec_dep = dependency ('lcevc_dec', required: get_option('lcevcdecoder'))

if lcevc_dec_dep.found()
  gstlcevcdecoder = library('gstlcevcdecoder',
    lcevcdecoder_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [
      gstpbutils_dep,
      gstvideo_dep,
      gstcodecparsers_dep,
      lcevc_dec_dep,
    ],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstlcevcdecoder]
endif
