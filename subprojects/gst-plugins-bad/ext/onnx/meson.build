onnx_sources = [
  'gstonnx.c',
  'gstonnxinference.cpp',
  'gstonnxclient.cpp',
]

onnx_headers = [
  'gstonnxinference.h',
  'gstml.h',
  'gstonnxclient.h',
]

doc_sources = []
foreach s: onnx_sources + onnx_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'onnx': pathsep.join(doc_sources)
}

if get_option('onnx').disabled()
  subdir_done()
endif

onnxrt_dep = dependency('libonnxruntime', version : '>= 1.16.1', required : get_option('onnx'))

extra_args = []
extra_deps = []
if gstcuda_dep.found()
    extra_args += ['-DHAVE_CUDA']
    extra_deps += [gstcuda_dep]
endif

if onnxrt_dep.found()
  gstonnx = library('gstonnx',
    onnx_sources,
    c_args : gst_plugins_bad_args + extra_args,
    cpp_args : gst_plugins_bad_args + extra_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc, cuda_stubinc],
    dependencies : [gstbase_dep, gstvideo_dep, gstanalytics_dep, onnxrt_dep,
        libm] + extra_deps,
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstonnx]
endif
