va_sources = [
  'plugin.c',
  'gstvaallocator.c',
  'gstvabasedec.c',
  'gstvabasetransform.c',
  'gstvacaps.c',
  'gstvadecoder.c',
  'gstvadeinterlace.c',
  'gstvadevice.c',
  'gstvadisplay_priv.c',
  'gstvafilter.c',
  'gstvah264dec.c',
  'gstvah265dec.c',
  'gstvapool.c',
  'gstvaprofile.c',
  'gstvasurfacecopy.c',
  'gstvautils.c',
  'gstvavideoformat.c',
  'gstvavp8dec.c',
  'gstvavp9dec.c',
  'gstvampeg2dec.c',
  'gstvavpp.c',
  'vasurfaceimage.c',
]

if not gstva_dep.found()
  subdir_done()
endif

libgudev_dep = dependency('gudev-1.0', required: false)
cdata.set10('HAVE_GUDEV', libgudev_dep.found())

if libva_dep.version().version_compare('>= 1.8')
  va_sources += 'gstvaav1dec.c'
endif

driverdir = libva_dep.get_variable('driverdir', default_value: '')
if driverdir == ''
  driverdir = join_paths(get_option('prefix'), get_option('libdir'), 'dri')
endif
gstva_cargs = ['-DLIBVA_DRIVERS_PATH="' + driverdir + '"']

gstva = library('gstva',
  va_sources,
  c_args : gst_plugins_bad_args + extra_c_args + gstva_cargs + ['-std=c99'],
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gstcodecs_dep, gstallocators_dep, gstva_dep, libgudev_dep] + extra_dep,
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstva, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstva]
