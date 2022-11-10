if get_option('onnx').disabled()
  subdir_done()
endif


onnxrt_dep = dependency('libonnxruntime', version : '>= 1.13.1', required : get_option('onnx'))

if onnxrt_dep.found()
	onnxrt_include_root = onnxrt_dep.get_variable('includedir')
	onnxrt_includes = [onnxrt_include_root / 'core/session', onnxrt_include_root / 'core']
	onnxrt_dep_args = []
	
	compiler = meson.get_compiler('cpp')
	if compiler.has_header(onnxrt_include_root / 'core/providers/cuda/cuda_provider_factory.h')
	  onnxrt_dep_args = ['-DGST_ML_ONNX_RUNTIME_HAVE_CUDA']
	endif
    gstonnx = library('gstonnx',
    'gstonnx.c',
    'gstonnxelement.c',
    'gstonnxobjectdetector.cpp',
    'gstonnxclient.cpp',
    c_args : gst_plugins_bad_args,
    cpp_args: onnxrt_dep_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc, onnxrt_includes],
    dependencies : [gstbase_dep, gstvideo_dep, onnxrt_dep, libm],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstonnx]
 endif
