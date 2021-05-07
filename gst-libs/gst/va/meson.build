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
libgudev_dep = dependency('gudev-1.0', required: va_option)
libdrm_dep = dependency('libdrm', required: false, fallback: ['libdrm', 'ext_libdrm'])

if not (libva_dep.found() and libva_drm_dep.found() and libgudev_dep.found())
  if va_option.enabled()
    error('The va lib was enabled explicity, but required dependencies were not found.')
  endif
  subdir_done()
endif


gstva = library('gstva-' + api_version,
  va_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_VA'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, libva_dep, libva_drm_dep, libgudev_dep, libdrm_dep],
)

pkgconfig.generate(gstva,
  libraries : gst_dep,
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : 'gstreamer-va-1.0',
  description : 'GStreamer VA support',
)

gstva_dep = declare_dependency(link_with : gstva,
  include_directories : [libsinc],
  dependencies : [gst_dep, libva_dep, libva_drm_dep, libgudev_dep, libdrm_dep])
meson.override_dependency('gstreamer-va-1.0', gstva_dep)
