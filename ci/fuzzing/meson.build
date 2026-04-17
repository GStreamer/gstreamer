if get_option('oss_fuzz').disabled()
  subdir_done()
endif

fuzz_targets = [
  ['gst-codec-utils.c', false, ['gstreamer-pbutils-1.0']],
  ['gst-discoverer.c', false, ['gstreamer-pbutils-1.0']],
  ['gst-subparse.c', false, []],
  ['gst-tag.c', false, ['gstreamer-tag-1.0']],
  ['typefind.c'],
]

extra_sources = []
gst_dep = dependency('gstreamer-1.0')
common_deps = [gst_dep]

cxx = meson.get_compiler('cpp')
fuzzing_engine = cxx.find_library('FuzzingEngine', required: false)
if fuzzing_engine.found()
  common_deps += fuzzing_engine
else
  extra_sources += ['localfuzzer.c']
endif

foreach target : fuzz_targets
  file_name = target.get(0)
  test_name = file_name.split('.').get(0)

  extra_deps = []
  if target.length() >= 3
    foreach d : target.get(2)
      extra_deps += dependency(d)
    endforeach
  endif

  skip_test = false
  if target.length() >= 2
    skip_test = target.get(1)
  endif
  if not skip_test
    exe = executable(test_name, [extra_sources, file_name],
      dependencies: common_deps + extra_deps,
    )
  endif
endforeach
