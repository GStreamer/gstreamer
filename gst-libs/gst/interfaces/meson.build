photography_sources = ['photography.c']
photo_headers = ['photography.h', 'photography-prelude.h']
install_headers(photo_headers, subdir : 'gstreamer-1.0/gst/interfaces')

mkenums = find_program('build_mkenum.py')

photoenum_h = custom_target('photoenum_h',
  output : 'photography-enumtypes.h',
  input : photo_headers,
  install : true,
  install_dir : 'include/gstreamer-1.0/gst/interfaces',
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

photoenum_c = custom_target('photoenum_c',
  output : 'photography-enumtypes.c',
  input : photo_headers,
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gstphotography = library('gstphotography-' + api_version,
  photography_sources, photoenum_h, photoenum_c,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep],
)

gstphotography_dep = declare_dependency(link_with : gstphotography,
  include_directories : [libsinc],
  dependencies : [gst_dep],
  sources : [photoenum_h])
