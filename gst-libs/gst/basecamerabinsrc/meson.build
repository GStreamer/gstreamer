camerabin_sources = [
  'gstcamerabin-enum.c',
  'gstcamerabinpreview.c',
  'gstbasecamerasrc.c',
]
camerabin_headers = [
  'basecamerabinsrc-prelude.h',
  'gstcamerabin-enum.h',
  'gstcamerabinpreview.h',
  'gstbasecamerasrc.h',
]
install_headers(camerabin_headers, subdir : 'gstreamer-1.0/gst/basecamerabinsrc')

gstbasecamerabin = library('gstbasecamerabinsrc-' + api_version,
  camerabin_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_BASE_CAMERA_BIN_SRC'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gstapp_dep],
)

_sources = []
if build_gir
  basecamerabin_gir = gnome.generate_gir(gstbasecamerabin,
    sources : camerabin_sources + camerabin_headers,
    namespace : 'GstBadBaseCameraBin',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-bad-base-camerabinsrc-1.0',
    includes : ['Gst-1.0', 'GstApp-1.0'],
    install : false, # Only for the documentation
    extra_args : gir_init_section + ['-DGST_USE_UNSTABLE_API'],
    dependencies : [gstapp_dep],
    build_by_default : true,
  )
  _sources += [basecamerabin_gir]
endif

gstbasecamerabin_dep = declare_dependency(link_with : gstbasecamerabin,
  include_directories : [libsinc],
  dependencies : [gstapp_dep])
