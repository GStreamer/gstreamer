rfbsrc_sources = [
  'gstrfbsrc.c',
  'gstrfb-utils.c',
  'rfbdecoder.c',
  'd3des.c',
]

rfbsrc_headers = [
  'd3des.h',
  'rfbdecoder.h',
  'gstrfb-utils.h',
  'gstrfbsrc.h',
]

doc_sources = []
foreach s: rfbsrc_sources + rfbsrc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'rfbsrc': pathsep.join(doc_sources)
}

if get_option('librfb').disabled()
  subdir_done()
endif

librfb_incs = include_directories('..')

gstrfbsrc = library('gstrfbsrc',
  rfbsrc_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc, librfb_incs],
  dependencies : [gstbase_dep, gstvideo_dep, gio_dep, x11_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstrfbsrc]
