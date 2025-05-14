hip_sources = [
  'gsthip-enums.cpp',
  'gsthipbasefilter.cpp',
  'gsthipbufferpool.cpp',
  'gsthipconverter.cpp',
  'gsthipconvertscale.cpp',
  'gsthipdevice.cpp',
  'gsthiploader.cpp',
  'gsthipmemory.cpp',
  'gsthipmemorycopy.cpp',
  'gsthiprtc.cpp',
  'gsthiputils.cpp',
  'plugin.cpp',
]

doc_sources = []
foreach s: hip_sources
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'hip': pathsep.join(doc_sources)
}

extra_args = [
  '-DGST_USE_UNSTABLE_API',
]

extra_deps = []
hip_amd_precompiled = []
hip_nvidia_precompiled = []

hip_option = get_option('hip')
if hip_option.disabled()
  subdir_done()
endif

if host_system not in ['linux', 'windows']
  subdir_done()
endif

hip_precompile_amd_opt = get_option('hip-amd-precompile')
hip_precompile_nvidia_opt = get_option('hip-nvidia-precompile')
have_hipcc = false
have_nvcc = false
if not hip_precompile_amd_opt.disabled() and not meson.is_cross_build()
  if host_system == 'windows'
    hipcc = find_program('hipcc.bin', required: false)
    if not hipcc.found()
      hip_root = run_command(python3, '-c', 'import os; print(os.environ.get("HIP_PATH"))', check: false).stdout().strip()
      if hip_root != '' and hip_root != 'None'
        hip_bin_path = join_paths(hip_root, 'bin')
        hipcc = find_program('hipcc.bin',
                              dirs: [hip_bin_path],
                              required: hip_precompile_amd_opt)
      endif
    endif
  else
    hipcc = find_program('hipcc', required: hip_precompile_amd_opt)
  endif
  have_hipcc = hipcc.found()
endif

if not hip_precompile_nvidia_opt.disabled() and not meson.is_cross_build()
  nvcc = find_program('nvcc', required: hip_precompile_nvidia_opt)
  have_nvcc = nvcc.found()
endif

hip_cdata = configuration_data()
if have_hipcc or have_nvcc
  hip_cdata.set('HIP_AMD_PRECOMPILED', have_hipcc)
  hip_cdata.set('HIP_NVIDIA_PRECOMPILED', have_nvcc)
  subdir('kernel')
endif

if gstcuda_dep.found()
  hip_cdata.set('HAVE_GST_CUDA', true)
  extra_deps += [gstcuda_dep]
endif

configure_file(
  output: 'gsthip-config.h',
  configuration: hip_cdata,
)

hip_incdir = include_directories('./stub')
gsthip = library('gsthip', hip_sources + hip_amd_precompiled + hip_nvidia_precompiled,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args: gst_plugins_bad_args + extra_args,
  include_directories : [configinc, hip_incdir],
  dependencies : [gstbase_dep, gstvideo_dep, gmodule_dep] + extra_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsthip]
