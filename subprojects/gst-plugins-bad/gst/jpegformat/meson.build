jpegf_sources = [
  'gstjpegformat.c',
  'gstjpegparse.c',
  'gstjifmux.c',
]

jpegf_headers = [
  'gstjpegparse.h',
  'gstjifmux.h',
]

doc_sources = []
foreach s: jpegf_sources + jpegf_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'jpegformat': pathsep.join(doc_sources)
}

if get_option('jpegformat').disabled()
  subdir_done()
endif

gstjpegformat = library('gstjpegformat',
  jpegf_sources,
  c_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstcodecparsers_dep, gstvideo_dep, gsttag_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstjpegformat]
