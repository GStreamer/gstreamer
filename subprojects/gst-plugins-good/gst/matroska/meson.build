matroska_sources = [
  'ebml-read.c',
  'ebml-write.c',
  'gstmatroskaelement.c',
  'matroska.c',
  'matroska-demux.c',
  'matroska-parse.c',
  'matroska-ids.c',
  'matroska-mux.c',
  'matroska-read-common.c',
  'webm-mux.c',
  'lzo.c',
]

matroska_headers = [
  'ebml-ids.h',
  'ebml-read.h',
  'webm-mux.h',
  'matroska-demux.h',
  'ebml-write.h',
  'matroska-ids.h',
  'matroska-parse.h',
  'gstmatroskaelements.h',
  'matroska-read-common.h',
  'matroska-mux.h',
  'lzo.h',
]

doc_sources = []
foreach s: matroska_sources + matroska_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'matroska': pathsep.join(doc_sources)
}

if get_option('matroska').disabled()
  subdir_done()
endif

bz2_dep = cc.find_library('bz2', required : get_option('bz2'))
cdata.set('HAVE_BZ2', bz2_dep.found() and cc.has_header('bzlib.h'))

gstmatroska = library('gstmatroska',
  matroska_sources,
  c_args : gst_plugins_good_args,
  link_args : noseh_link_args,
  include_directories : [configinc],
  dependencies : [gstpbutils_dep, gstaudio_dep, gstriff_dep,
                  gstvideo_dep, gsttag_dep, gstbase_dep,
                  gst_dep, zlib_dep, bz2_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmatroska]
