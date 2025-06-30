lcevcdecoder_sources = [
  'plugin.c',
  'gstlcevcdecutils.c',
  'gstlcevcdec.c',
  'gstlcevcdecodebin.c',
  'gstlcevch264decodebin.c',
  'gstlcevch265decodebin.c',
  'gstlcevch266decodebin.c',
]

lcevcdecoder_headers = [
  'gstlcevcdec.h',
  'gstlcevch264decodebin.h',
  'gstlcevch265decodebin.h',
  'gstlcevch266decodebin.h',
  'gstlcevcdecutils.h',
  'gstlcevcdecodebin.h',
]

doc_sources = []
foreach s: lcevcdecoder_sources + lcevcdecoder_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'lcevcdecoder': pathsep.join(doc_sources)
}

lcevc_dec_dep = dependency ('lcevc_dec', version: '>= 4.0.1', required: get_option('lcevcdecoder'))

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
