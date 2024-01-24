if get_option('harnesses').disabled()
  subdir_done()
endif

harnesses = [
  ['test_smokedec.c']
]

incdir = include_directories('../subprojects/gst-plugins-good/ext/jpeg')

extra_sources = []
gst_dep = dependency('gstreamer-1.0')
common_deps = [gst_dep]

#cxx = meson.get_compiler('cpp')
#fuzzing_engine = cxx.find_library('FuzzingEngine', required: true)

foreach harness : harnesses
  file_name = harness.get(0)
  test_name = file_name.split('.').get(0)

  extra_deps = []
  if harness.length() >= 3
    extra_deps = dependency(target.get(2))
  endif

  exe = executable(test_name, [extra_sources, file_name],
    include_directories: incdir,
    dependencies: common_deps + extra_deps,
    c_args: ['-fsanitize=fuzzer,address'],
    link_args: ['-fsanitize=fuzzer,address']
  )
endforeach
