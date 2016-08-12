matroska_sources = [
  'ebml-read.c',
  'ebml-write.c',
  'matroska.c',
  'matroska-demux.c',
  'matroska-parse.c',
  'matroska-ids.c',
  'matroska-mux.c',
  'matroska-read-common.c',
  'webm-mux.c',
  'lzo.c',
]

gstmatroska = library('gstmatroska',
  matroska_sources,
  c_args : gst_plugins_good_args,
  link_args : noseh_link_args,
  include_directories : [configinc],
  dependencies : [gstpbutils_dep, gstaudio_dep, gstriff_dep,
                  gstvideo_dep, gsttag_dep, gstbase_dep,
                  gst_dep, zlib_dep, bz2lib, libm],
  install : true,
  install_dir : plugins_install_dir,
)
