rtsp_sources = [
  'gstrtsptransport.c',
  'gstrtspurl.c',
  'gstrtspmessage.c',
  'gstrtspconnection.c',
  'gstrtspdefs.c',
  'gstrtspextension.c',
  'gstrtsprange.c'
  ]

rtsp_headers = [
  'gstrtspconnection.h',
  'gstrtspdefs.h',
  'gstrtspextension.h',
  'gstrtsp.h',
  'gstrtspmessage.h',
  'gstrtsprange.h',
  'gstrtsptransport.h',
  'gstrtspurl.h',
  'rtsp-prelude.h',
  'rtsp.h',
]
install_headers(rtsp_headers, subdir : 'gstreamer-1.0/gst/rtsp/')

rtsp_enums = gnome.mkenums_simple('gstrtsp-enumtypes',
  sources : rtsp_headers,
  body_prefix : '#ifdef HAVE_CONFIG_H\n#include "config.h"\n#endif',
  header_prefix : '#include <gst/rtsp/rtsp-prelude.h>',
  decorator : 'GST_RTSP_API',
  install_header: true,
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst/rtsp'))
gstrtsp_c = rtsp_enums[0]
gstrtsp_h = rtsp_enums[1]

if host_system == 'windows'
  winsock2 = cc.find_library('ws2_32')
else
  winsock2 = []
endif
gstrtsp_deps = [gst_base_dep, gst_dep, gio_dep, libm, winsock2]

gst_rtsp = library('gstrtsp-@0@'.format(api_version),
  rtsp_sources,
  gstrtsp_h, gstrtsp_c,
  c_args : gst_plugins_base_args + ['-DBUILDING_GST_RTSP'],
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : gstrtsp_deps,
)

rtsp_gen_sources = [gstrtsp_h]
if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/rtsp/rtsp.h' ]
  rtsp_gen_sources += [gnome.generate_gir(gst_rtsp,
    sources : rtsp_sources + rtsp_headers + [gstrtsp_c] + [gstrtsp_h],
    namespace : 'GstRtsp',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-rtsp-1.0',
    includes : ['Gst-1.0', 'GstBase-1.0', 'GstSdp-1.0', 'Gio-2.0'],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : gstrtsp_deps + [sdp_dep]
  )]
endif

rtsp_dep = declare_dependency(link_with : gst_rtsp,
  include_directories : [libsinc],
  dependencies : gstrtsp_deps,
  sources : rtsp_gen_sources)
