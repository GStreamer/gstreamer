nvcodec_sources = [
  'gstcudabasetransform.c',
  'gstcudaconverter.c',
  'gstcudaconvertscale.c',
  'gstcudafilter.c',
  'gstcudamemorycopy.c',
  'gstcuvidloader.c',
  'gstnvav1dec.c',
  'gstnvbaseenc.c',
  'gstnvdec.c',
  'gstnvdecoder.c',
  'gstnvenc.c',
  'gstnvencoder.cpp',
  'gstnvh264enc.c',
  'gstnvh264encoder.cpp',
  'gstnvh265enc.c',
  'gstnvh265encoder.cpp',
  'gstnvh264dec.c',
  'gstnvh265dec.c',
  'gstnvvp8dec.c',
  'gstnvvp9dec.c',
  'plugin.c',
]

nvmm_sources = [
  'gstcudanvmm.c',
]

if get_option('nvcodec').disabled()
  subdir_done()
endif

if not gstcuda_dep.found()
  if get_option('nvcodec').enabled()
    error('The nvcodec was enabled explicitly, but required gstcuda dependency is not found')
  endif
  subdir_done()
endif

plugin_incdirs = [configinc, cuda_stubinc]
extra_args = ['-DGST_USE_UNSTABLE_API']

if gstgl_dep.found()
  extra_args += ['-DHAVE_NVCODEC_GST_GL=1']
endif

if gstd3d11_dep.found()
  extra_args += ['-DGST_CUDA_HAS_D3D=1', '-DCOBJMACROS']
endif

if host_system == 'linux'
  have_nvmm = false
  if cc.has_header('nvbufsurface.h')
    have_nvmm = true
  elif cc.has_header('/opt/nvidia/deepstream/deepstream/sources/includes/nvbufsurface.h')
    # XXX: Should add an option for SDK path??
    have_nvmm = true
    plugin_incdirs += [include_directories('/opt/nvidia/deepstream/deepstream/sources/includes')]
  endif

  if have_nvmm
    extra_args += ['-DHAVE_NVCODEC_NVMM']
    nvcodec_sources += nvmm_sources
  endif
endif

if cc.get_id() != 'msvc'
  if host_system == 'windows'
    # MinGW 32bits compiler seems to be complaining about redundant-decls
    # when ComPtr is in use. Let's just disable the warning
    extra_args += cc.get_supported_arguments([
      '-Wno-redundant-decls',
    ])
  endif

  # Allow deprecated decls since it's part of SDK header
  extra_args += cc.get_supported_arguments([
    '-Wno-deprecated-declarations',
  ])
endif

gstnvcodec = library('gstnvcodec',
  nvcodec_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args : gst_plugins_bad_args + extra_args,
  include_directories : plugin_incdirs,
  dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, gstgl_dep, gstglproto_dep, gmodule_dep, gstcodecs_dep, gstd3d11_dep, gstcuda_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstnvcodec]

