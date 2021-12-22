jpegf_sources = [
  'gstjpegformat.c',
  'gstjpegparse.c',
  'gstjifmux.c',
]

gstjpegformat = library('gstjpegformat',
  jpegf_sources,
  c_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstcodecparsers_dep, gsttag_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstjpegformat, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstjpegformat]
