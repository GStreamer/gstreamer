gstiosurface_dep = dependency('', required: false)

if host_system != 'darwin'
  subdir_done()
endif

iosurface_headers = files(
  'iosurface-prelude.h',
  'gstiosurface.h',
)

iosurface_sources = files(
  'gstiosurface.c',
)

doc_sources = []
foreach s: iosurface_sources + iosurface_headers
  doc_sources += s.full_path()
endforeach

libs_sources += {
  'iosurface': pathsep.join(doc_sources)
}

iosurface_dep = dependency('IOSurface', required: false)
if not iosurface_dep.found()
  subdir_done()
endif

gstiosurface = library('gstiosurface-' + api_version,
  iosurface_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API',
      '-DBUILDING_GST_IOSURFACE', '-DG_LOG_DOMAIN="GStreamer-IOSurface"'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gst_dep, iosurface_dep],
)

pkg_name = 'gstreamer-iosurface-1.0'
library_def = {'lib': gstiosurface}

pkgconfig.generate(gstiosurface,
  libraries : [gst_dep],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'GStreamer IOSurface support',
)

gst_libraries += [[pkg_name, library_def]]

install_headers(iosurface_headers, subdir : 'gstreamer-1.0/gst/iosurface')

gstiosurface_dep = declare_dependency(link_with : gstiosurface,
  include_directories : [libsinc],
  dependencies : [gst_dep, iosurface_dep])

meson.override_dependency(pkg_name, gstiosurface_dep)
