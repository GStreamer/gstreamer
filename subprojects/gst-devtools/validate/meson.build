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

# Symbol visibility
if cc.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'c')
endif

configure_file(output : 'config.h', configuration : cdata)

validate_plugins_install_dir = '@0@/gstreamer-1.0/validate'.format(get_option('libdir'))
subdir('data')
subdir('gst')
subdir('gst-libs')
subdir('launcher')
subdir('tools')
if not get_option('tests').disabled()
  subdir('tests')
endif
subdir('plugins')
#subdir('po')
