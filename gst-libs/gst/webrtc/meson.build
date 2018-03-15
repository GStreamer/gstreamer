webrtc_sources = [
  'dtlstransport.c',
  'icetransport.c',
  'rtcsessiondescription.c',
  'rtpreceiver.c',
  'rtpsender.c',
  'rtptransceiver.c',
]

webrtc_headers = [
  'dtlstransport.h',
  'icetransport.h',
  'rtcsessiondescription.h',
  'rtpreceiver.h',
  'rtpsender.h',
  'rtptransceiver.h',
  'webrtc_fwd.h',
  'webrtc.h',
]

webrtc_enumtypes_headers = [
  'dtlstransport.h',
  'icetransport.h',
  'rtptransceiver.h',
  'webrtc_fwd.h',
]

mkenums = find_program('webrtc_mkenum.py')
gstwebrtc_h = custom_target('gstwebrtcenum_h',
  output : 'webrtc-enumtypes.h',
  input : webrtc_enumtypes_headers,
  install : true,
  install_dir : 'include/gstreamer-1.0/gst/webrtc/',
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gstwebrtc_c = custom_target('gstwebrtcenum_c',
  output : 'webrtc-enumtypes.c',
  input : webrtc_enumtypes_headers,
  depends : [gstwebrtc_h],
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])
webrtc_gen_sources = [gstwebrtc_h]

gstwebrtc_dependencies = [gstbase_dep, gstsdp_dep]

gstwebrtc = library('gstwebrtc-' + api_version,
  webrtc_sources, gstwebrtc_c, gstwebrtc_h,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : gstwebrtc_dependencies,
)

if build_gir
  webrtc_gir = gnome.generate_gir(gstwebrtc,
    sources : webrtc_sources + webrtc_headers + [gstwebrtc_h],
    namespace : 'GstWebRTC',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-webrtc-1.0',
    includes : ['Gst-1.0', 'GstSdp-1.0'],
    install : true,
    extra_args : gir_init_section + ['-DGST_USE_UNSTABLE_API'] + ['--c-include=gst/webrtc/webrtc.h'],
    dependencies : [gstbase_dep, gstsdp_dep]
  )
  webrtc_gen_sources += webrtc_gir
endif

install_headers(webrtc_headers, subdir : 'gstreamer-1.0/gst/webrtc')

gstwebrtc_dep = declare_dependency(link_with: gstwebrtc,
  include_directories : libsinc,
  sources: webrtc_gen_sources,
  dependencies: gstwebrtc_dependencies)
