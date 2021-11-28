dxva_sources = [
  'gstdxvaav1decoder.cpp',
  'gstdxvah264decoder.cpp',
  'gstdxvah265decoder.cpp',
  'gstdxvampeg2decoder.cpp',
  'gstdxvautils.cpp',
  'gstdxvavp8decoder.cpp',
  'gstdxvavp9decoder.cpp',
]

extra_args = [
  '-DGST_USE_UNSTABLE_API',
  '-DBUILDING_GST_DXVA',
  '-DG_LOG_DOMAIN="GStreamer-Dxva"',
]

if host_system != 'windows'
  subdir_done()
endif

if not cc.has_header('dxva.h') or not cc.has_header('d3d9.h')
  subdir_done()
endif

# MinGW 32bits compiler seems to be complaining about redundant-decls
# when ComPtr is in use. Let's just disable the warning
if cc.get_id() != 'msvc'
  extra_mingw_args = cc.get_supported_arguments([
    '-Wno-redundant-decls',
  ])

  extra_args += extra_mingw_args
endif

pkg_name = 'gstreamer-dxva-' + api_version
gstdxva = library('gstdxva-' + api_version,
  dxva_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args : gst_plugins_bad_args + extra_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gstvideo_dep, gstcodecs_dep]
)

library_def = {'lib': gstdxva}
gst_libraries += [[pkg_name, library_def]]

# Still non-public api, should not install headers
gstdxva_dep = declare_dependency(link_with : gstdxva,
  include_directories : [libsinc],
  dependencies : [gstvideo_dep, gstcodecs_dep])

meson.override_dependency(pkg_name, gstdxva_dep)