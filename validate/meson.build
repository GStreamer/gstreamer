inc_dirs = include_directories('.')

json_dep = dependency('json-glib-1.0')
cdata = configuration_data()
cdata.set('GST_API_VERSION', '"@0@"'.format(apiversion))
cdata.set('VALIDATEPLUGINDIR', '"@0@/@1@/gstreamer-1.0/validate"'.format(get_option('prefix'),get_option('libdir')))
cdata.set('GST_DATADIR', '"@0@/@1@"'.format(prefix, get_option('datadir')))
cdata.set('PACKAGE_NAME', '"GStreamer Validate"')
configure_file(input : 'config.h.meson',
  output : 'config.h',
  configuration : cdata)

subdir('data')
subdir('gst')
subdir('launcher')
subdir('tools')
subdir('docs')
subdir('pkgconfig')
subdir('tests')
#subdir('po')
