examples = [
  'test-client-managed-mikey',
  'test-onvif',
]

foreach example : examples
  executable(example, '@0@.c'.format(example),
    dependencies: [gst_dep],
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    install: false)
endforeach
