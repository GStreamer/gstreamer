
if not gstgl_dep.found()
  if get_option('wpe').enabled()
    error('wpe plugin enabled but GL support was not detected')
  endif
  subdir_done()
endif

wpe_dep = dependency('wpe-webkit-1.1', version : '>= 2.28', required : false)
if not wpe_dep.found()
  wpe_dep = dependency('wpe-webkit-1.0', version : '>= 2.28', required : get_option('wpe'))
endif
wpe_fdo_dep = dependency('wpebackend-fdo-1.0', version : '>= 1.8', required : get_option('wpe'))
egl_dep = dependency('egl', required : get_option('wpe'))
xkbcommon_dep = dependency('xkbcommon', version : '>= 0.8', required : get_option('wpe'))
wl_server_dep = dependency('wayland-server', required : get_option('wpe'))

if not wpe_dep.found() or not wpe_fdo_dep.found() or not egl_dep.found() or not xkbcommon_dep.found()
  subdir_done()
endif

wpe_extension_install_dir = get_option('prefix') / get_option('libdir') / meson.project_name() / 'wpe-extension'

giounix_dep = dependency('gio-unix-2.0', required: false)
gstwpe = library('gstwpe',
  ['WPEThreadedView.cpp', 'gstwpe.cpp', 'gstwpevideosrc.cpp', 'gstwpesrcbin.cpp'],
  dependencies : [egl_dep, wpe_dep, wpe_fdo_dep, gstallocators_dep, gstaudio_dep, gstvideo_dep, gstbase_dep, gstgl_dep, xkbcommon_dep, wl_server_dep, giounix_dep],
  cpp_args : gst_plugins_bad_args + ['-DHAVE_CONFIG_H=1', '-DWPE_EXTENSION_INSTALL_DIR=' + wpe_extension_install_dir],
  include_directories : [configinc],
  install : true,
  install_dir : plugins_install_dir)

if giounix_dep.found()
  subdir('wpe-extension')
endif
pkgconfig.generate(gstwpe, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstwpe]
