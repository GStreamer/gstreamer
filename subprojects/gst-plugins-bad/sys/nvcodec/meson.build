nvcodec_sources = [
  'plugin.c',
  'gstnvenc.c',
  'gstnvbaseenc.c',
  'gstnvh264enc.c',
  'gstnvh265enc.c',
  'gstcudaloader.c',
  'gstnvdec.c',
  'gstcuvidloader.c',
  'gstcudacontext.c',
  'gstcudautils.c',
  'gstnvdecoder.c',
  'gstnvh264dec.c',
  'gstnvh265dec.c',
  'gstcudamemory.c',
  'gstcudabufferpool.c',
  'gstcudabasetransform.c',
  'gstcudamemorycopy.c',
  'gstcudanvrtc.c',
  'gstnvrtcloader.c',
  'cuda-converter.c',
  'gstcudafilter.c',
  'gstcudabasefilter.c',
  'gstcudaconvert.c',
  'gstcudascale.c',
  'gstnvvp8dec.c',
  'gstnvvp9dec.c',
]

nvmm_sources = [
  'gstcudanvmm.c',
]

if get_option('nvcodec').disabled()
  subdir_done()
endif

plugin_incdirs = [configinc, include_directories('./stub')]
extra_c_args = ['-DGST_USE_UNSTABLE_API']

if gstgl_dep.found()
  extra_c_args += ['-DHAVE_NVCODEC_GST_GL=1']
endif

if gstd3d11_dep.found()
  extra_c_args += ['-DHAVE_NVCODEC_GST_D3D11=1', '-DCOBJMACROS']
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
    extra_c_args += ['-DHAVE_NVCODEC_NVMM']
    nvcodec_sources += nvmm_sources
  endif
endif

gstnvcodec = library('gstnvcodec',
  nvcodec_sources,
  c_args : gst_plugins_bad_args + extra_c_args,
  include_directories : plugin_incdirs,
  dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, gstgl_dep, gstglproto_dep, gmodule_dep, gstcodecs_dep, gstd3d11_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstnvcodec, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstnvcodec]

