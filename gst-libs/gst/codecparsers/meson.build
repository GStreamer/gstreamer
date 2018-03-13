codecparser_sources = [
  'gstjpeg2000sampling.c',
  'gstjpegparser.c',
  'gstmpegvideoparser.c',
  'gsth264parser.c',
  'gstvc1parser.c',
  'gstmpeg4parser.c',
  'gsth265parser.c',
  'gstvp8parser.c',
  'gstvp8rangedecoder.c',
  'gstvp9parser.c',
  'vp9utils.c',
  'parserutils.c',
  'nalutils.c',
  'dboolhuff.c',
  'vp8utils.c',
  'gstmpegvideometa.c',
]
codecparser_headers = [
  'codecparsers-prelude.h',
  'gstmpegvideoparser.h',
  'gsth264parser.h',
  'gstvc1parser.h',
  'gstmpeg4parser.h',
  'gsth265parser.h',
  'gstvp8parser.h',
  'gstvp8rangedecoder.h',
  'gstjpeg2000sampling.h',
  'gstjpegparser.h',
  'gstmpegvideometa.h',
  'gstvp9parser.h',
]
install_headers(codecparser_headers, subdir : 'gstreamer-1.0/gst/codecparsers')

cp_args = [
  '-DGST_USE_UNSTABLE_API',
  '-Dvp8_norm=gst_codecparsers_vp8_norm',
  '-Dvp8dx_start_decode=gst_codecparsers_vp8dx_start_decode',
  '-Dvp8dx_bool_decoder_fill=gst_codecparsers_vp8dx_bool_decoder_fill',
]

gstcodecparsers = library('gstcodecparsers-' + api_version,
  codecparser_sources,
  c_args : gst_plugins_bad_args + cp_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gstbase_dep, libm],
)

gstcodecparsers_dep = declare_dependency(link_with : gstcodecparsers,
  include_directories : [libsinc],
  dependencies : [gstbase_dep])
