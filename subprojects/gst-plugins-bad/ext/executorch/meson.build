executorch_sources = [
  'gstexecutorch.c',
  'gstexecutorchinference.cpp',
]

executorch_headers = [
  'gstexecutorchinference.h',
]

doc_sources = []
foreach s: executorch_sources + executorch_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'executorch': pathsep.join(doc_sources)
}

if get_option('executorch').disabled()
  subdir_done()
endif

executorch_dep = dependency('ExecuTorch',
  required : get_option('executorch'),
  method : 'cmake',
  modules : ['executorch_core'],
  static : false,
)

if not executorch_dep.found()
  subdir_done()
endif

executorch_lib = cc.find_library('executorch', required : get_option('executorch'))
executorch_core_lib = cc.find_library('executorch_core', required : get_option('executorch'))

if not executorch_lib.found() or not executorch_core_lib.found()
  if get_option('executorch').enabled()
    error('ExecuTorch libraries not found. Install ExecuTorch (libexecutorch, libexecutorch_core) or set the appropriate library path.')
  endif
  subdir_done()
endif

executorch_operator_deps = [
    cc.find_library('portable_ops_lib', required : get_option('executorch')),
    cc.find_library('portable_kernels', required : get_option('executorch')),
    cc.find_library('extension_data_loader', required : get_option('executorch')),
]

# Optional XNNPACK backend for accelerated inference
xnnpack_backend_lib = cc.find_library('xnnpack_backend', required : false)
if xnnpack_backend_lib.found()
  executorch_operator_deps += [
    xnnpack_backend_lib,
    cc.find_library('XNNPACK', required : true),
    cc.find_library('xnnpack-microkernels-prod', required : true),
    cc.find_library('extension_threadpool', required : true),
    cc.find_library('pthreadpool', required : true),
    cc.find_library('cpuinfo', required : false),
  ]
endif

gstexecutorch = library('gstexecutorch',
  executorch_sources,
  c_args : gst_plugins_bad_args,
  cpp_args : ['-std=c++17'],
  link_args : noseh_link_args + [
    '-Wl,--no-as-needed',
    '-Wl,--whole-archive',
    '-lexecutorch',
    '-lportable_ops_lib',
  ] + (xnnpack_backend_lib.found() ? ['-lxnnpack_backend'] : []) + [
    '-Wl,--no-whole-archive',
  ],

  include_directories : [
    configinc,
    libsinc,
  ],
  dependencies : [
    gstbase_dep,
    gstvideo_dep,
    gstanalytics_dep,
    libm,
    executorch_dep,
    executorch_lib,
    executorch_core_lib,
  ] + executorch_operator_deps,
  install : true,
  install_dir : plugins_install_dir
)
plugins += [gstexecutorch]
