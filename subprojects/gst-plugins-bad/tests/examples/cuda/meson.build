if not gstcuda_dep.found()
  subdir_done()
endif

gl_header_dep = dependency('', fallback : ['gl-headers', 'gl_headers_dummy_dep'],
    required : false)
if gl_header_dep.type_name() == 'internal'
  # this will only contain the includes of headers that are not found
  compat_includes = subproject('gl-headers').get_variable('compatibility_includes')
else
  compat_includes = []
endif

executable('cudamemory-sync', 'cudamemory-sync.c',
  include_directories : [configinc, cuda_stubinc] + compat_includes,
  dependencies: [gst_dep, gstvideo_dep, gstcuda_dep, gl_header_dep],
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  install: false)

executable('cuda-template',
  [
    '../key-handler.c',
    'cuda-template.c',
    'template-plugin/cuda-transform-ip-template.c',
    'template-plugin/plugin.c',
  ],
  include_directories : [configinc, cuda_stubinc] + compat_includes,
  dependencies: [gst_dep, gstvideo_dep, gstcuda_dep, gl_header_dep],
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  install: false)

executable('cudamemorypool', 'cudamemorypool.c',
  include_directories : [configinc, cuda_stubinc],
  dependencies: [gst_dep, gstvideo_dep, gstcuda_dep],
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  install: false)

executable('nvenc-extern-pool', 'nvenc-extern-pool.c',
  include_directories : [configinc, cuda_stubinc],
  dependencies: [gst_dep, gstvideo_dep, gstcuda_dep],
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  install: false)
