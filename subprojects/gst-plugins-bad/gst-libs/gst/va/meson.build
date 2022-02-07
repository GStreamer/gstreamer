va_sources = [
  'gstvadisplay.c',
  'gstvadisplay_drm.c',
  'gstvadisplay_wrapped.c',
]

va_headers = [
  'gstvadisplay.h',
  'gstvadisplay_drm.h',
  'gstvadisplay_wrapped.h',
  'va_fwd.h',
  'va-prelude.h',
]

gstva_dep = dependency('', required : false)

va_option = get_option('va')
if va_option.disabled() or host_system != 'linux'
  subdir_done()
endif

libva_req = ['>= 1.6']
libva_dep = dependency('libva', version: libva_req, required: va_option)
libva_drm_dep = dependency('libva-drm', version: libva_req, required: va_option)

if not (libva_dep.found() and libva_drm_dep.found())
  subdir_done()
endif

libdrm_dep = dependency('libdrm', required: false, fallback: ['libdrm', 'ext_libdrm'])
cdata.set10('HAVE_LIBDRM', libdrm_dep.found())

gstva = library('gstva-' + api_version,
  va_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_VA', '-DG_LOG_DOMAIN="GStreamer-VA"'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, libva_dep, libva_drm_dep, libdrm_dep],
)

pkg_name = 'gstreamer-va-' + api_version
libraries += [[pkg_name, {'lib': gstva}]]

gstva_dep = declare_dependency(link_with : gstva,
  include_directories : [libsinc],
  dependencies : [gst_dep, libva_dep, libva_drm_dep, libdrm_dep])
meson.override_dependency(pkg_name, gstva_dep)
