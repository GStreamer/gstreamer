tsdemux_sources = [
  'mpegtspacketizer.c',
  'mpegtsbase.c',
  'mpegtsparse.c',
  'tsdemux.c',
  'gsttsdemux.c',
  'pesparse.c',
]

tsdemux_headers = [
  'tsdemux.h',
  'gstmpegdesc.h',
  'mpegtsparse.h',
  'pesparse.h',
  'mpegtspacketizer.h',
  'mpegtsbase.h',
  'gstmpegdefs.h',
]

doc_sources = []
foreach s: tsdemux_sources + tsdemux_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'mpegtsdemux': pathsep.join(doc_sources)
}

if get_option('mpegtsdemux').disabled()
  subdir_done()
endif

gstmpegtsdemux = library('gstmpegtsdemux',
  tsdemux_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  dependencies : [gstcodecparsers_dep, gstmpegts_dep, gsttag_dep,
                  gstpbutils_dep, gstaudio_dep, gstbase_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmpegtsdemux]
