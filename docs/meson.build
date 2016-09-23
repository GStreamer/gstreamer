docconf = configuration_data()

docconf.set('GST_API_VERSION', apiversion)
docconf.set('PACKAGE_VERSION', gst_version)
docconf.set('PLUGINDIR', '@0@/lib/gstreamer-1.0'.format(get_option('prefix')))

version_entities = configure_file(input : 'version.entities.in',
  output : 'version.entities',
  configuration : docconf)

subdir('gst')
subdir('libs')
