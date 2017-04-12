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
  'rtsp.h',
]
install_headers(rtsp_headers, subdir : 'gstreamer-1.0/gst/rtsp/')

mkenums = find_program('rtsp_mkenum.py')

gstrtsp_h = custom_target('gstrtspenum_h',
  output : 'gstrtsp-enumtypes.h',
  input : rtsp_headers,
  install : true,
  install_dir : 'include/gstreamer-1.0/gst/rtsp/',
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gstrtsp_c = custom_target('gstrtspenum_c',
  output : 'gstrtsp-enumtypes.c',
  input : rtsp_headers,
  depends : [gstrtsp_h],
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

winsock2 = cc.find_library('ws2_32', required : false)

gstrtsp_deps = [gst_dep, gio_dep, libm, winsock2]
gst_rtsp = library('gstrtsp-@0@'.format(api_version),
  rtsp_sources,
  gstrtsp_h, gstrtsp_c,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : gstrtsp_deps,
  vs_module_defs: vs_module_defs_dir + 'libgstrtsp.def',
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
    includes : ['Gst-1.0', 'GstSdp-1.0', 'Gio-2.0'],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : gstrtsp_deps + [sdp_dep]
  )]
endif

rtsp_dep = declare_dependency(link_with : gst_rtsp,
  include_directories : [libsinc],
  dependencies : gstrtsp_deps,
  sources : rtsp_gen_sources)
