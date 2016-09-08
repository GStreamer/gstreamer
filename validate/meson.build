inc_dirs = include_directories('.')

json_dep = dependency('json-glib-1.0')
cdata = configuration_data()

unwind_dep = dependency('libunwind', required : false)
dw_dep = dependency('libdw', required: false)
if unwind_dep.found()
  cdata.set('HAVE_UNWIND', 1)
  if dw_dep.found()
    cdata.set('HAVE_DW', 1)
  else
    message('Support for backtraces is partial only.')
  endif
else
  if cc.has_function('backtrace')
    cdata.set('HAVE_BACKTRACE', 1)
  else
      message('NO backtraces support.')
  endif
endif


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
