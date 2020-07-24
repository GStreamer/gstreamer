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
]

if get_option('nvcodec').disabled()
  subdir_done()
endif

plugin_incdirs = [configinc, include_directories('./stub')]
extra_c_args = ['-DGST_USE_UNSTABLE_API']

if gstgl_dep.found()
  extra_c_args += ['-DHAVE_NVCODEC_GST_GL=1']
endif

gstnvcodec = library('gstnvcodec',
  nvcodec_sources,
  c_args : gst_plugins_bad_args + extra_c_args,
  include_directories : plugin_incdirs,
  dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, gstgl_dep, gstglproto_dep, gmodule_dep, gstcodecs_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstnvcodec, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstnvcodec]

