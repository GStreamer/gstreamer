if not gstvulkan_dep.found()
  subdir_done()
endif

executable('vulkanenc',
  ['vulkanenc.c', '../key-handler.c'],
  include_directories : [configinc],
  dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  install: false)
