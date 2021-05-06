va_sources = [
  'plugin.c',
  'gstvaallocator.c',
  'gstvabasedec.c',
  'gstvacaps.c',
  'gstvadecoder.c',
  'gstvadevice.c',
  'gstvadisplay_priv.c',
  'gstvafilter.c',
  'gstvah264dec.c',
  'gstvah265dec.c',
  'gstvapool.c',
  'gstvaprofile.c',
  'gstvautils.c',
  'gstvavideoformat.c',
  'gstvavp8dec.c',
  'gstvavp9dec.c',
  'gstvampeg2dec.c',
  'gstvavpp.c'
]

va_option = get_option('va')
if va_option.disabled() or host_system != 'linux'
  subdir_done()
endif

if not gstva_dep.found()
  if va_option.enabled()
    error('The va plugin was enabled explicity, but required dependencies were not found.')
  endif
  subdir_done()
endif

libva_av1_req = ['>= 1.8']
libva_av1_dep = dependency('libva', version: libva_av1_req, required: va_option)
if libva_av1_dep.found()
  va_sources += 'gstvaav1dec.c'
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
  dependencies : [libva_dep, gstvideo_dep, gstcodecs_dep, gstallocators_dep, gstva_dep] + extra_dep,
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstva, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstva]
