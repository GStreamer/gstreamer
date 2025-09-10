if not gstvulkan_dep.found()
  subdir_done()
endif

executable('vulkanenc',
  ['vulkanenc.c', '../key-handler.c'],
  include_directories : [configinc],
  dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  install: false)

sdl3_dep = dependency('sdl3', version : '>=3.2.0', required : get_option('examples'))

if sdl3_dep.found()
  executable('sdl3_vulkandec',
    ['sdl3_vulkandec.c'],
    include_directories : [configinc],
    dependencies: [gst_dep, gstapp_dep, gstvulkan_dep, sdl3_dep],
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    install: false)
endif
