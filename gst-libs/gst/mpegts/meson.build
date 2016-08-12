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
  'mpegts.h',
]
install_headers(mpegts_headers, subdir : 'gstreamer-1.0/gst/mpegts')

mkenums = find_program('mpegts_enum.py')

mpegts_h = custom_target('mpegtsenum_h',
  output : 'gstmpegts-enumtypes.h',
  input : mpegts_headers,
  install : true,
  install_dir : 'include/gstreamer-1.0/gst/mpegts',
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

mpegts_c = custom_target('mpegtsenum_c',
  output : 'gstmpegts-enumtypes.c',
  input : mpegts_headers,
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gstmpegts = library('gstmpegts-' + api_version,
  mpegts_sources, mpegts_h, mpegts_c,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep],
  vs_module_defs: vs_module_defs_dir + 'libgstmpegts.def',
)
# TODO: generate gir

gstmpegts_dep = declare_dependency(link_with : gstmpegts,
  include_directories : [libsinc],
  dependencies : [gst_dep],
  sources : [mpegts_h])
