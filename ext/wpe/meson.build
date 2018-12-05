
if not gstgl_dep.found()
  if get_option('wpe').enabled()
    error('wpe plugin enabled but GL support was not detected')
  endif
  subdir_done()
endif

wpe_dep = dependency('wpe-webkit-0.1', required : get_option('wpe'))
wpe_fdo_dep = dependency('wpebackend-fdo-0.1', required : get_option('wpe'))
egl_dep = dependency('egl', required : get_option('wpe'))
xkbcommon_dep = dependency('xkbcommon', version : '>= 0.8', required : get_option('wpe'))

if not wpe_dep.found() or not wpe_fdo_dep.found() or not egl_dep.found() or not xkbcommon_dep.found()
  subdir_done()
endif

gstwpe = library('gstwpe',
  ['WPEThreadedView.cpp', 'gstwpesrc.cpp'],
  dependencies : [egl_dep, wpe_dep, wpe_fdo_dep, gstvideo_dep, gstbase_dep, gstgl_dep, xkbcommon_dep],
  cpp_args : gst_plugins_bad_args + ['-DHAVE_CONFIG_H=1'],
  include_directories : [configinc],
  install : true,
  install_dir : plugins_install_dir)
pkgconfig.generate(gstwpe, install_dir : plugins_pkgconfig_install_dir)
