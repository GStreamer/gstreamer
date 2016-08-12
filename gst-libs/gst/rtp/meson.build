rtp_sources = [
  'gstrtpbuffer.c',
  'gstrtcpbuffer.c',
  'gstrtppayloads.c',
  'gstrtphdrext.c',
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
  'gstrtppayloads.h',
  'rtp.h',
]
install_headers(rtp_headers, subdir : 'gstreamer-1.0/gst/rtp/')

mkenums = find_program('rtp_mkenum.py')

gstrtp_enum_h = custom_target('gstrtp_enum_h',
  output : 'gstrtp-enumtypes.h',
  input : rtp_headers,
  install : true,
  install_dir : 'include/gstreamer-1.0/gst/rtp/',
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gstrtp_enum_c = custom_target('gstrtp_enum_c',
  output : 'gstrtp-enumtypes.c',
  input : rtp_headers,
  depends : [gstaudio_h, gstrtp_enum_h],
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gst_rtp = library('gstrtp-@0@'.format(api_version),
  rtp_sources, gstrtp_enum_c, gstrtp_enum_h,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [audio_dep, gst_base_dep],
  vs_module_defs: vs_module_defs_dir + 'libgstrtp.def',
)
# TODO: generate gir

rtp_dep = declare_dependency(link_with : gst_rtp,
  include_directories : [libsinc],
  dependencies : [audio_dep, gst_base_dep],
  sources : [gstrtp_enum_h])
