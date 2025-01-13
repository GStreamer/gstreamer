mxf_sources = [
  'mxf.c',
  'gstmxfelement.c',
  'mxful.c',
  'mxftypes.c',
  'mxfmetadata.c',
  'mxfessence.c',
  'mxfquark.c',
  'mxfmux.c',
  'mxfdemux.c',
  'mxfaes-bwf.c',
  'mxfmpeg.c',
  'mxfdv-dif.c',
  'mxfalaw.c',
  'mxfjpeg2000.c',
  'mxfd10.c',
  'mxfup.c',
  'mxfvc3.c',
  'mxfprores.c',
  'mxfvanc.c',
  'mxfcustom.c',
  'mxfffv1.c',
#  'mxfdms1.c',
]

mxf_headers = [
  'mxfmetadata.h',
  'mxfd10.h',
  'mxfvanc.h',
  'mxfdemux.h',
  'mxftypes.h',
  'mxfdms1.h',
  'mxfdv-dif.h',
  'mxfjpeg2000.h',
  'mxful.h',
  'mxfmpeg.h',
  'mxfquark.h',
  'mxfvc3.h',
  'mxfcustom.h',
  'mxfffv1.h',
  'mxfaes-bwf.h',
  'mxfup.h',
  'mxfprores.h',
  'gstmxfelements.h',
  'mxfessence.h',
  'mxfalaw.h',
  'mxfmux.h',
]

doc_sources = []
foreach s: mxf_sources + mxf_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mxf': pathsep.join(doc_sources)
}

if get_option('mxf').disabled()
  subdir_done()
endif

gstmxf = library('gstmxf',
  mxf_sources,
  c_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstaudio_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmxf]
