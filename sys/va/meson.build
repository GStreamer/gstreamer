va_sources = [
  'plugin.c',
  'gstvaallocator.c',
  'gstvacaps.c',
  'gstvadecoder.c',
  'gstvadisplay.c',
  'gstvadisplay_drm.c',
  'gstvadisplay_wrapped.c',
  'gstvadevice.c',
  'gstvah264dec.c',
  'gstvapool.c',
  'gstvaprofile.c',
  'gstvautils.c',
  'gstvavideoformat.c',
]

va_option = get_option('va')
if va_option.disabled()
  subdir_done()
endif

libva_req = ['>= 1.6']

libva_dep = dependency('libva', version: libva_req, required: va_option)
libva_drm_dep = dependency('libva-drm', version: libva_req, required: va_option)
libgudev_dep = dependency('gudev-1.0', required: va_option)
libdrm_dep = dependency('libdrm', required: false,
			fallback: ['libdrm', 'ext_libdrm'])

have_va = libva_dep.found() and libva_drm_dep.found()
if not (have_va and libgudev_dep.found())
  if va_option.enabled()
    error('The va plugin was enabled explicity, but required dependencies were not found.')
  endif
  subdir_done()
endif

cdata.set10('HAVE_LIBDRM', libdrm_dep.found())

driverdir = libva_dep.get_pkgconfig_variable('driverdir')
if driverdir == ''
  driverdir = join_paths(get_option('prefix'), get_option('libdir'), 'dri')
endif
gstva_cargs = ['-DLIBVA_DRIVERS_PATH="' + driverdir + '"']

gstva = library('gstva',
  va_sources,
  c_args : gst_plugins_bad_args + extra_c_args + gstva_cargs + ['-std=c99'],
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstcodecs_dep, libva_dep, gstallocators_dep, libva_drm_dep, libgudev_dep, libdrm_dep] + extra_dep,
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstva, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstva]
