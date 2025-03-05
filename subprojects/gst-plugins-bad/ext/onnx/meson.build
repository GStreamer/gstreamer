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

extra_args = []
extra_deps = []
extra_incs = []

onnxrt_dep = dependency('libonnxruntime', version : '>= 1.16.1',
			required: false)
if not onnxrt_dep.found()
  fsmod = import('fs')
  sysroot = meson.get_external_property('sys_root', '/')
  onnx_inc = join_paths(sysroot, 'usr/include/onnxruntime')

  incs = []
  if fsmod.is_dir(onnx_inc)
    incs = [include_directories(onnx_inc)]
  endif

  onnxrt_dep = cxx.find_library('onnxruntime',
				has_headers: 'onnxruntime_cxx_api.h',
				header_include_directories: incs,
				required: get_option('onnx'))
  extra_incs += incs
endif

if not onnxrt_dep.found()
  subdir_done()
endif

if gstcuda_dep.found()
    extra_args += ['-DHAVE_CUDA']
    extra_deps += [gstcuda_dep]
endif

if cxx.has_header('core/providers/cpu/cpu_provider_factory.h',
		  dependencies: onnxrt_dep,
		  include_directories: extra_incs)
  extra_args += ['-DCPUPROVIDER_IN_SUBDIR']
endif

if cxx.has_function('OrtSessionOptionsAppendExecutionProvider_VSINPU',
		    dependencies: onnxrt_dep) and \
   cxx.has_header('core/providers/vsinpu/vsinpu_provider_factory.h',
		    dependencies: onnxrt_dep,
		    include_directories: extra_incs)
  message('Enabled VSI Onnx VSI NPU provider')
  extra_args += ['-DHAVE_VSI_NPU']
endif

gstonnx = library('gstonnx',
  onnx_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args : gst_plugins_bad_args + extra_args,
  link_args : noseh_link_args,
  include_directories : [configinc, libsinc, cuda_stubinc] + extra_incs,
  dependencies : [gstbase_dep, gstvideo_dep, gstanalytics_dep, onnxrt_dep,
      libm] + extra_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstonnx]
