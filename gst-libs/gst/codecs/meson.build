codecs_sources = files([
  'gsth264decoder.c',
  'gsth264picture.c',
  'gsth265decoder.c',
  'gsth265picture.c',
  'gstvp9decoder.c',
  'gstvp9picture.c',
  'gstvp8decoder.c',
  'gstvp8picture.c',
])

codecs_headers = [
  'gsth264decoder.h',
  'gsth264picture.h',
  'gsth265decoder.h',
  'gsth265picture.h',
  'gstvp9decoder.h',
  'gstvp9picture.h',
  'gstvp8decoder.h',
  'gstvp8picture.h',
]

cp_args = [
  '-DGST_USE_UNSTABLE_API',
  '-DBUILDING_GST_CODECS',
]

gstcodecs = library('gstcodecs-' + api_version,
  codecs_sources,
  c_args : gst_plugins_bad_args + cp_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gstvideo_dep, gstcodecparsers_dep],
)

gen_sources = []
if build_gir
  codecs_gir = gnome.generate_gir(gstcodecs,
    sources : codecs_sources + codecs_headers,
    namespace : 'GstCodecs',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-codecs-1.0',
    includes : ['Gst-1.0', 'GstVideo-1.0'],
    install : true,
    extra_args : gir_init_section + ['-DGST_USE_UNSTABLE_API'] +
      ['--c-include=gst/codecs/gsth264decoder.h',
      '--c-include=gst/codecs/gsth265decoder.h',
      '--c-include=gst/codecs/gstvp9decoder.h',
      '--c-include=gst/codecs/gstvp8decoder.h',
      ],
    dependencies : [gstvideo_dep, gstcodecparsers_dep]
  )
endif

gstcodecs_dep = declare_dependency(link_with : gstcodecs,
  include_directories : [libsinc],
  sources: gen_sources,
  dependencies : [gstvideo_dep, gstcodecparsers_dep])
