examples = ['gstmotioncells_dynamic_test', 'gsthanddetect_test', 'gstfacedetect_test']

foreach example : examples
  exe_name = example
  src_file = '@0@.c'.format(example)

  executable(exe_name,
    src_file,
    include_directories: [configinc],
    dependencies: [glib_dep, gst_dep, gstbase_dep],
    c_args: ['-DHAVE_CONFIG_H=1'],
    install: false)
endforeach
