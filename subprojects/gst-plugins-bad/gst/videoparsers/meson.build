vparse_sources = [
  'plugin.c',
  'gstvideoparserselement.c',
  'h263parse.c',
  'gsth263parse.c',
  'gstdiracparse.c',
  'dirac_parse.c',
  'gsth264parse.c',
  'gstmpegvideoparse.c',
  'gstmpeg4videoparse.c',
  'gstpngparse.c',
  'gstvc1parse.c',
  'gsth265parse.c',
  'gstvideoparseutils.c',
  'gstjpeg2000parse.c',
  'gstvp9parse.c',
  'gstav1parse.c',
  'gsth266parse.c',
]

vparse_headers = [
  'gstvc1parse.h',
  'gstmpeg4videoparse.h',
  'gstvideoparserselements.h',
  'gstmpegvideoparse.h',
  'gsth264parse.h',
  'gstjpeg2000parse.h',
  'gstvp9parse.h',
  'gsth263parse.h',
  'gstdiracparse.h',
  'gstvideoparseutils.h',
  'gstav1parse.h',
  'gstpngparse.h',
  'gsth265parse.h',
  'gsth266parse.h',
  'dirac_parse.h',
  'h263parse.h',
]

doc_sources = []
foreach s: vparse_sources + vparse_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'videoparsersbad': pathsep.join(doc_sources)
}

if get_option('videoparsers').disabled()
  subdir_done()
endif

gstvideoparsersbad = library('gstvideoparsersbad',
  vparse_sources,
  c_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
  include_directories : [configinc],
  dependencies : [gstcodecparsers_dep, gstbase_dep, gstpbutils_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstvideoparsersbad]
