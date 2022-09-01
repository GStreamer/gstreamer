va_sources = [
  'gstvaallocator.c',
  'gstvadisplay.c',
  'gstvadisplay_drm.c',
  'gstvadisplay_wrapped.c',
  'gstvapool.c',
  'gstvasurfacecopy.c',
  'gstvautils.c',
  'gstvavideoformat.c',
  'vasurfaceimage.c',
]

va_headers = [
  'gstva.h',
  'gstvaallocator.h',
  'gstvadisplay.h',
  'gstvadisplay_drm.h',
  'gstvadisplay_wrapped.h',
  'gstvapool.h',
  'gstvautils.h',
  'gstvavideoformat.h',
  'va-prelude.h',
  'va_fwd.h',
]

gstva_dep = dependency('', required : false)

if host_system != 'linux'
  subdir_done()
endif

libva_req = ['>= 1.6']
libva_dep = dependency('libva', version: libva_req, required: false,
    fallback: ['libva', 'libva_dep'])
libva_drm_dep = dependency('libva-drm', version: libva_req, required: false,
    fallback: ['libva', 'libva_drm_dep'])

if not (libva_dep.found() and libva_drm_dep.found())
  subdir_done()
endif

libdrm_dep = dependency('libdrm', required: false, fallback: ['libdrm', 'ext_libdrm'])
cdata.set10('HAVE_LIBDRM', libdrm_dep.found())

va_enums = gnome.mkenums_simple('va-enumtypes',
  sources: ['gstva.h'],
  decorator: 'GST_VA_API',
  header_prefix: '#include <gst/va/va-prelude.h>',
  body_prefix: '#ifdef HAVE_CONFIG_H\n#include "config.h"\n#endif',
  install_header: false)

va_enumtypes_h = va_enums[1]

gstva = library('gstva-' + api_version,
  va_sources, va_enums,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_VA', '-DG_LOG_DOMAIN="GStreamer-VA"'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gstallocators_dep, libva_dep, libva_drm_dep, libdrm_dep],
)

pkg_name = 'gstreamer-va-' + api_version
gst_libraries += [[pkg_name, {'lib': gstva}]]

gstva_dep = declare_dependency(link_with : gstva,
  include_directories : [libsinc],
  dependencies : [gst_dep, gstvideo_dep, gstallocators_dep, libva_dep, libva_drm_dep, libdrm_dep],
  sources : [va_enumtypes_h])

meson.override_dependency(pkg_name, gstva_dep)
