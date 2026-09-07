onnx_sources = [
  'gstonnx.c',
  'gstonnx-utils.c',
  'gstonnxinference.c',
  'gstonnximporter.c',
  'gstonnximporter-cpu.c',
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

extra_args = ['-DGST_USE_UNSTABLE_API']
extra_deps = []
extra_incs = []
have_winml = false
have_directml = false
onnx_require_eps = get_option('onnx-require-eps')
have_gst_hip = false

fsmod = import('fs')

onnxrt_dep = dependency('', required : false)

onnx_winml_opt = get_option('onnx-winml')
if host_system == 'windows' and not onnx_winml_opt.disabled()
  onnxrt_dep = dependency('microsoft.windows.ai.machinelearning',
                          version: '>= 2.2.12', allow_fallback: true,
                          required: onnx_winml_opt)
  if onnxrt_dep.found()
    have_winml = true
    onnx_sources += ['gstonnx-winml.c']
    if gstd3d12_dep.found() and cc.has_header('d3d12.h') and cc.has_header('directml.h')
      dml_dep = cc.find_library('directml', required: false)
      if dml_dep.found()
        have_directml = true
        extra_deps += [gstd3d12_dep, dml_dep]
        onnx_sources += ['gstonnx-dml.cpp']
      endif
    endif
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

if host_system != 'windows'
  extra_deps += cc.find_library('dl', required: false)
endif

if 'cuda' in onnx_require_eps and not gstcuda_dep.found()
  error('ONNX EP "cuda" enabled, but CUDA was not found')
endif

if 'vsi' in onnx_require_eps
  cc.has_function('OrtSessionOptionsAppendExecutionProvider_VSINPU',
                  dependencies: onnxrt_dep)
endif

if gsthip_dep.found()
  have_gst_hip = true
  extra_deps += [gsthip_dep]
  extra_incs += [hipstub_incdir]
  onnx_sources += ['gstonnximporter-hip.c']
endif

onnxrt_libdir = onnxrt_dep.get_variable(pkgconfig: 'libdir', default_value: '')
ort_api_version = cc.get_define(
    'ORT_API_VERSION',
    prefix: '#include <onnxruntime_c_api.h>',
    dependencies: onnxrt_dep)
message('ORT API version: ' + ort_api_version)
ort_api_version = ort_api_version.to_int()

if not have_winml
  if 'migraphx' in onnx_require_eps
    cc.find_library('onnxruntime_providers_migraphx', dirs: [onnxrt_libdir])
  endif

  if 'vitisai' in onnx_require_eps and ort_api_version < 18
    error('VitisAI EP requires ONNX Runtime >= 1.18 headers')
  endif

  if 'hip' in onnx_require_eps and ort_api_version < 22
    error('HIP EP requires ONNX Runtime >= 1.22 headers')
  endif
endif

model_cache_dir = meson.build_root() / 'ort-model-cache'
meson.add_devenv({
  'GST_ORT_MODEL_CACHE_DIR': model_cache_dir,
})

if onnxrt_libdir != ''
  if host_system == 'windows'
    meson.add_devenv({'PATH': onnxrt_libdir}, method: 'prepend')
  elif host_system == 'darwin'
    meson.add_devenv({'DYLD_LIBRARY_PATH': onnxrt_libdir}, method: 'prepend')
  else
    meson.add_devenv({'LD_LIBRARY_PATH': onnxrt_libdir}, method: 'prepend')
  endif
endif

onnx_config = configuration_data()
onnx_config.set10('HAVE_WINML', have_winml)
onnx_config.set10('HAVE_DIRECTML', have_directml)
onnx_config.set10('HAVE_GST_HIP', have_gst_hip)

configure_file(
  output: 'gstonnx-config.h',
  configuration: onnx_config,
)

# Don't link to libonnxruntime, we dlopen it. We can't strip link_args on winml
# because the dep contains WinML libs in addition to onnxruntime. The linker
# will strip unused import libraries, so that's fine.
if not have_winml
  onnxrt_dep = onnxrt_dep.partial_dependency(
      compile_args: true,
      includes: true,
  )
endif

gstonnx = library('gstonnx',
  onnx_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args : gst_plugins_bad_args + extra_args,
  link_args : noseh_link_args,
  include_directories : [configinc, libsinc, cuda_stubinc] + extra_incs,
  dependencies : [gstbase_dep, gstvideo_dep, gstanalytics_dep, onnxrt_dep,
      libm] + extra_deps,
  override_options : ['cpp_std=c++17'],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstonnx]
