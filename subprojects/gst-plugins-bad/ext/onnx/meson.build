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
have_winml = false
have_vsi_npu = false
have_migraphx = false
have_ort_register_execution_provider_library = false
have_cuda = false

fsmod = import('fs')

onnxrt_dep = dependency('', required : false)

onnx_winml_opt = get_option('onnx-winml')
if host_system == 'windows' and not onnx_winml_opt.disabled()
  onnxrt_dep = dependency('microsoft.windows.ai.machinelearning',
      version: '>= 2.2.12',
      allow_fallback: true,
      required: onnx_winml_opt)
  if onnxrt_dep.found()
    have_winml = true
    have_ort_register_execution_provider_library = true
    onnx_sources += ['gstonnx-winml.c']
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
    have_cuda = true
    extra_deps += [gstcuda_dep]
endif

if not have_winml
  onnxrt_libdir = onnxrt_dep.get_variable(pkgconfig: 'libdir', default_value: '')

  if cc.has_function('OrtSessionOptionsAppendExecutionProvider_VSINPU',
          dependencies: onnxrt_dep) and \
    cc.has_header('core/providers/vsinpu/vsinpu_provider_factory.h',
          dependencies: onnxrt_dep,
          include_directories: extra_incs)
    have_vsi_npu = true
  endif

  migraphx_dep = cc.find_library('onnxruntime_providers_migraphx',
      dirs: [onnxrt_libdir],
      required: false)
  if migraphx_dep.found()
    have_migraphx = true
  endif

  # Check if OrtApi::RegisterExecutionProviderLibrary is available as needed
  # for dynamically loaded execution providers.
  if cc.has_member('OrtApi', 'RegisterExecutionProviderLibrary',
          prefix: '#include <onnxruntime_c_api.h>',
          dependencies: onnxrt_dep,
          include_directories: extra_incs)
    have_ort_register_execution_provider_library = true
  endif
endif


features = {
  'cuda': {
    'enabled': have_cuda,
    'message': 'Enabled CUDA execution provider',
  },
  'winml': {
    'enabled': have_winml,
    'message': 'Enabled Windows ML',
  },
  'vsi_npu': {
    'enabled': have_vsi_npu,
    'message': 'Enabled VSI Onnx VSI NPU provider',
  },
  'migraphx': {
    'enabled': have_migraphx,
    'message': 'Enabled ONNX MIGraphX execution provider'
  },
  'ort_register_execution_provider_library': {
    'enabled': have_ort_register_execution_provider_library,
    'message': 'Found ONNX Runtime >= 1.22, enabling dynamically loaded execution providers',
  }
}

onnx_config = configuration_data()
foreach name, feature : features
  enabled = feature['enabled']
  onnx_config.set10('HAVE_' + name.underscorify().to_upper(), enabled)
  if enabled
    message(feature['message'])
  endif
endforeach

configure_file(
  output: 'gstonnx-config.h',
  configuration: onnx_config,
)

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
