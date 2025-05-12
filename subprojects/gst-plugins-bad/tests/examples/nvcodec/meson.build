executable('nvcodec',
  ['nvcodec.c', '../key-handler.c'],
  include_directories : [configinc],
  dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  install: false)

executable('cudaipc', ['cudaipc.c'],
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
  install: false)

cuda_runtime_dep = dependency('cuda', version : '>=11', required: false)
if not cuda_runtime_dep.found()
  subdir_done()
endif

if gstcuda_dep.found()
  executable('cuda-appsrc', ['cuda-appsrc.c'],
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies: [gst_dep, gstbase_dep, gstapp_dep, gstvideo_dep, gstcuda_dep, cuda_runtime_dep],
    install: false)
endif