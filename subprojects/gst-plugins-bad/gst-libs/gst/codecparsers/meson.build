codecparser_sources = files([
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
  'gstav1parser.c',
  'gsth266parser.c',
  'gsth264bitwriter.c',
  'gsth265bitwriter.c',
  'gstav1bitwriter.c',
  'gstvp9bitwriter.c',
  'gstjpegbitwriter.c',
  'gstlcevcmeta.c',
])
codecparser_headers = files([
  'codecparsers-prelude.h',
  'gstmpegvideoparser.h',
  'gsth264bitwriter.h',
  'gsth264parser.h',
  'gstvc1parser.h',
  'gstmpeg4parser.h',
  'gsth265parser.h',
  'gstvp8parser.h',
  'gstvp8rangedecoder.h',
  'gstjpeg2000sampling.h',
  'gstjpegbitwriter.h',
  'gstjpegparser.h',
  'gstmpegvideometa.h',
  'gstvp9parser.h',
  'gstav1bitwriter.h',
  'gstav1parser.h',
  'gstlcevcmeta.h',
  'gsth266parser.h',
])
install_headers(codecparser_headers, subdir : 'gstreamer-1.0/gst/codecparsers')

doc_sources = []
foreach s: codecparser_sources + codecparser_headers
  doc_sources += s.full_path()
endforeach

libs_sources += {
  'codecparsers': pathsep.join(doc_sources)
}

cp_args = [
  '-DGST_USE_UNSTABLE_API',
  '-DBUILDING_GST_CODEC_PARSERS',
  '-DG_LOG_DOMAIN="GStreamer-CodecParsers"',
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
  darwin_versions : osxversion,
  install : true,
  dependencies : [gstbase_dep],
)

pkg_name = 'gstreamer-codecparsers-1.0'
pkgconfig.generate(gstcodecparsers,
  libraries : [gst_dep, gstbase_dep],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'Bitstream parsers for GStreamer elements',
)

gen_sources = []
if build_gir
  gir = {
    'sources' : codecparser_sources + codecparser_headers,
    'namespace' : 'GstCodecParsers',
    'nsversion' : api_version,
    'identifier_prefix' : 'Gst',
    'symbol_prefix' : 'gst',
    'export_packages' : pkg_name,
    'includes' : ['Gst-1.0', 'GstBase-1.0'],
    'install' : true,
    'extra_args' : gir_init_section + cp_args +
      ['--c-include=gst/codecs/gstav1decoder.h',
       '--c-include=gst/codecparsers/gstav1bitwriter.h',
       '--c-include=gst/codecparsers/gstav1parser.h',
       '--c-include=gst/codecparsers/gsth264bitwriter.h',
       '--c-include=gst/codecparsers/gsth264parser.h',
       '--c-include=gst/codecparsers/gsth265parser.h',
       '--c-include=gst/codecparsers/gsth266parser.h',
       '--c-include=gst/codecparsers/gstjpeg2000sampling.h',
       '--c-include=gst/codecparsers/gstjpegbitwriter.h',
       '--c-include=gst/codecparsers/gstjpegparser.h',
       '--c-include=gst/codecparsers/gstlcevcmeta.h',
       '--c-include=gst/codecparsers/gstmpeg4parser.h',
       '--c-include=gst/codecparsers/gstmpegvideometa.h',
       '--c-include=gst/codecparsers/gstmpegvideoparser.h',
       '--c-include=gst/codecparsers/gstvc1parser.h',
       '--c-include=gst/codecparsers/gstvp8parser.h',
       '--c-include=gst/codecparsers/gstvp8rangedecoder.h',
       '--c-include=gst/codecparsers/gstvp9parser.h',
      ],
    'dependencies' : [gstbase_dep]
  }
  library_def += {'gir': [gir]}
  if not static_build
    codecparsers_gir = gnome.generate_gir(gstcodecparsers, kwargs: gir)
    library_def += {'gir_targets':  library_def.get('gir_targets', []) + [codecparsers_gir]}
    gen_sources += codecparsers_gir
  endif
endif
gst_libraries += [[pkg_name, library_def]]


gstcodecparsers_dep = declare_dependency(link_with : gstcodecparsers,
  include_directories : [libsinc],
  sources: gen_sources,
  dependencies : [gstbase_dep])

gst_libraries += [[pkg_name, {'lib': gstcodecparsers}]]
meson.override_dependency(pkg_name, gstcodecparsers_dep)
