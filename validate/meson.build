inc_dirs = include_directories('.')

cdata = configuration_data()

cdata.set('GST_LICENSE', '"LGPL"')
cdata.set('VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE', '"gst-validate"')
cdata.set('GST_PACKAGE_NAME', '"GStreamer Validate"')
cdata.set('GST_PACKAGE_ORIGIN', '"Unknown package origin"')
cdata.set('GST_API_VERSION', '"@0@"'.format(apiversion))
cdata.set('VALIDATEPLUGINDIR', '"@0@/@1@/gstreamer-1.0/validate"'.format(get_option('prefix'),get_option('libdir')))
cdata.set('GST_DATADIR', '"@0@/@1@"'.format(prefix, get_option('datadir')))
cdata.set('PACKAGE_NAME', '"GStreamer Validate"')
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
if cc.has_header('unistd.h')
  cdata.set('HAVE_UNISTD_H', 1)
endif
configure_file(output : 'config.h', configuration : cdata)

vs_module_defs_dir = meson.current_source_dir() + '/win32/common/'

validate_plugins_install_dir = '@0@/gstreamer-1.0/validate'.format(get_option('libdir'))
subdir('data')
subdir('gst')
subdir('gst-libs')
subdir('launcher')
subdir('tools')
if build_machine.system() == 'windows'
  message('Disabling gtk-doc while building on Windows')
elif not get_option('gtk_doc')
  message('gtk-doc is disabled via options')
else
  if find_program('gtkdoc-scan', required : false).found()
    subdir('docs')
  else
    message('Not building documentation as gtk-doc was not found')
  endif
endif
subdir('pkgconfig')
subdir('tests')
subdir('plugins')
#subdir('po')
