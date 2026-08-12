onnx_sources = [
  'gstonnx.c',
  'gstonnxinference.c',
]

onnx_headers = [
  'gstonnxinference.h',
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

fsmod = import('fs')

onnxrt_dep = dependency('', required : false)

onnx_winml_opt = get_option('onnx-winml')
use_windows_ml = false
if host_system == 'windows' and not onnx_winml_opt.disabled()
  onnxrt_dep = dependency('microsoft.windows.ai.machinelearning',
      version: '>= 2.2.12',
      allow_fallback: true,
      required: onnx_winml_opt)
  if onnxrt_dep.found()
    use_windows_ml = true
  endif
endif

if not onnxrt_dep.found()
  onnxrt_dep = dependency('libonnxruntime', version : '>= 1.16.1',
                           required: false)
endif

if not onnxrt_dep.found()
  sysroot = meson.get_external_property('sys_root', '/')
  onnx_inc = join_paths(sysroot, 'usr/include/onnxruntime')

  incs = []
  if fsmod.is_dir(onnx_inc)
    incs = [include_directories(onnx_inc)]
  endif

  onnxrt_dep = cc.find_library('onnxruntime',
      has_headers: 'onnxruntime_c_api.h',
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

if not use_windows_ml
  onnxrt_libdir = onnxrt_dep.get_variable(pkgconfig: 'libdir', default_value: '')

  if cc.has_function('OrtSessionOptionsAppendExecutionProvider_VSINPU',
          dependencies: onnxrt_dep) and \
    cc.has_header('core/providers/vsinpu/vsinpu_provider_factory.h',
          dependencies: onnxrt_dep,
          include_directories: extra_incs)
    message('Enabled VSI Onnx VSI NPU provider')
    extra_args += ['-DHAVE_VSI_NPU']
  endif

  migraphx_dep = cc.find_library('onnxruntime_providers_migraphx',
      dirs: [onnxrt_libdir],
      required: false)
  if migraphx_dep.found()
    message('Enabled ONNX MIGraphX execution provider')
    extra_args += ['-DHAVE_MIGRAPHX']
  endif

  # Check if OrtApi::RegisterExecutionProviderLibrary is available as needed
  # for dynamically loaded execution providers.
  if cc.has_member('OrtApi', 'RegisterExecutionProviderLibrary',
          prefix: '#include <onnxruntime_c_api.h>',
          dependencies: onnxrt_dep,
          include_directories: extra_incs)
    message('Found ONNX Runtime >= 1.22, enabling dynamically loaded execution providers')
    extra_args += ['-DHAVE_ORT_REGISTER_EXECUTION_PROVIDER_LIBRARY']
  endif
endif

gstonnx = library('gstonnx',
  onnx_sources,
  c_args : gst_plugins_bad_args + extra_args,
  link_args : noseh_link_args,
  include_directories : [configinc, libsinc, cuda_stubinc] + extra_incs,
  dependencies : [gstbase_dep, gstvideo_dep, gstanalytics_dep, onnxrt_dep,
      libm] + extra_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstonnx]
