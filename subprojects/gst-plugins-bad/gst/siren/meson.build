siren_sources = [
  'gstsiren.c',
  'gstsirendec.c',
  'gstsirenenc.c',
  'common.c',
  'dct4.c',
  'decoder.c',
  'encoder.c',
  'huffman.c',
  'rmlt.c',
]

siren_headers = [
  'gstsirendec.h',
  'gstsirenenc.h',
  'gstsiren.h',
  'encoder.h',
  'huffman.h',
  'rmlt.h',
  'dct4.h',
  'common.h',
  'decoder.h',
  'siren7.h',
  'huffman_consts.h',
]

doc_sources = []
foreach s: siren_sources + siren_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'siren': pathsep.join(doc_sources)
}

if get_option('siren').disabled()
  subdir_done()
endif

gstsiren = library('gstsiren',
  siren_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstsiren]
