mpegts_sources = [
  'gstmpegtssection.c',
  'gstmpegtsdescriptor.c',
  'gst-dvb-descriptor.c',
  'gst-dvb-section.c',
  'gst-atsc-section.c',
]

mpegts_headers = [
  'gstmpegtssection.h',
  'gst-atsc-section.h',
  'gst-dvb-section.h',
  'gst-scte-section.h',
  'gstmpegtsdescriptor.h',
  'gst-dvb-descriptor.h',
  'mpegts-prelude.h',
  'mpegts.h',
]
install_headers(mpegts_headers, subdir : 'gstreamer-1.0/gst/mpegts')

mpegts_enums = gnome.mkenums_simple('gstmpegts-enumtypes',
  sources : mpegts_headers,
  body_prefix : '#ifdef HAVE_CONFIG_H\n#include "config.h"\n#endif',
  header_prefix : '#include <gst/mpegts/mpegts-prelude.h>',
  decorator : 'GST_MPEGTS_API',
  install_header: true,
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst/mpegts'))

mpegtsenumtypes_h = mpegts_enums[1]

gen_sources = [mpegtsenumtypes_h]

gstmpegts = library('gstmpegts-' + api_version,
  mpegts_sources, mpegts_enums,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_MPEGTS'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gst_dep],
)
if build_gir
  mpegts_gir = gnome.generate_gir(gstmpegts,
    sources : mpegts_sources + mpegts_headers,
    namespace : 'GstMpegts',
    nsversion : api_version,
    identifier_prefix : 'GstMpegts',
    symbol_prefix : ['gst_mpegts', 'gst'],
    export_packages : 'gstreamer-mpegts-1.0',
    includes : ['Gst-1.0'],
    install : true,
    extra_args : gir_init_section + ['-DGST_USE_UNSTABLE_API'] + ['--c-include=gst/mpegts/mpegts.h'],
    dependencies : [gst_dep]
  )
  gen_sources += mpegts_gir
endif


gstmpegts_dep = declare_dependency(link_with : gstmpegts,
  include_directories : [libsinc],
  dependencies : [gst_dep],
  sources : gen_sources)
