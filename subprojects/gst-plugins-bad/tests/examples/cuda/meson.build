if not gstcuda_dep.found()
  subdir_done()
endif

cuda_deps = []
cuda_inc_dir = []
if host_system == 'windows'
  cuda_path = run_command(python3, '-c', 'import os; print(os.environ.get("CUDA_PATH"))', check: false).stdout().strip()
  if cuda_path in ['', 'None']
    subdir_done()
  endif

  cuda_inc_dir = include_directories(join_paths(cuda_path, 'include'))
  if not cc.has_header('cuda.h', include_directories: cuda_inc_dir)
    subdir_done()
  endif
else
  # NOTE: meson dependency('cuda', version: '>=10'...) will return CUDA runtime
  # library but we want to use CUDA driver API. Iterates
  cuda_versions = [
    '12.4',
    '12.3',
    '12.2',
    '12.1',
    '12.0',
    '11.8',
    '11.8',
    '11.7',
    '11.6',
    '11.5',
    '11.4',
    '11.2',
    '11.1',
    '11.0',
    '10.2',
    '10.1',
    '10.0',
  ]

  cuda_dep = dependency('', required: false)
  foreach ver : cuda_versions
    cuda_dep = dependency('cuda-@0@'.format(ver), required: false)
    if cuda_dep.found()
      break
    endif
  endforeach

  if not cuda_dep.found()
    subdir_done()
  endif

  cuda_deps = [cuda_dep.partial_dependency(includes : true, compile_args: true)]
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
  include_directories : [configinc] + compat_includes + cuda_inc_dir,
  dependencies: [gst_dep, gstvideo_dep, gstcuda_dep, gl_header_dep] + cuda_deps,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  install: false)
