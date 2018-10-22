rtp_sources = [
  'gstrtpbuffer.c',
  'gstrtcpbuffer.c',
  'gstrtppayloads.c',
  'gstrtphdrext.c',
  'gstrtpmeta.c',
  'gstrtpbaseaudiopayload.c',
  'gstrtpbasepayload.c',
  'gstrtpbasedepayload.c'
  ]

rtp_headers = [
  'gstrtcpbuffer.h',
  'gstrtpbaseaudiopayload.h',
  'gstrtpbasedepayload.h',
  'gstrtpbasepayload.h',
  'gstrtpbuffer.h',
  'gstrtpdefs.h',
  'gstrtphdrext.h',
  'gstrtpmeta.h',
  'gstrtppayloads.h',
  'rtp-prelude.h',
  'rtp.h',
]
install_headers(rtp_headers, subdir : 'gstreamer-1.0/gst/rtp/')

rtp_enums = gnome.mkenums_simple('gstrtp-enumtypes',
  sources : rtp_headers,
  body_prefix : '#ifdef HAVE_CONFIG_H\n#include "config.h"\n#endif',
  header_prefix : '#include <gst/rtp/rtp-prelude.h>',
  decorator : 'GST_RTP_API',
  install_header: true,
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst/rtp'))
gstrtp_enum_c = rtp_enums[0]
gstrtp_enum_h = rtp_enums[1]

gstrtp_deps = [audio_dep, gst_base_dep]
gst_rtp = library('gstrtp-@0@'.format(api_version),
  rtp_sources, gstrtp_enum_c, gstrtp_enum_h,
  c_args : gst_plugins_base_args + ['-DBUILDING_GST_RTP'],
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : gstrtp_deps,
)

rtp_gen_sources = [gstrtp_enum_h]
if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/rtp/rtp.h' ]
  rtp_gir = gnome.generate_gir(gst_rtp,
    sources : rtp_sources + rtp_headers + [gstrtp_enum_c] + [gstrtp_enum_h],
    namespace : 'GstRtp',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-rtp-1.0',
    includes : ['Gst-1.0', 'GstBase-1.0'],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : gstrtp_deps
  )

  rtp_gen_sources += [rtp_gir]
endif


rtp_dep = declare_dependency(link_with : gst_rtp,
  include_directories : [libsinc],
  dependencies : gstrtp_deps,
  sources : rtp_gen_sources)
