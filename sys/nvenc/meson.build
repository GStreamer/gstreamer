nvenc_sources = [
  'gstnvbaseenc.c',
  'gstnvenc.c',
  'gstnvh264enc.c',
  'gstnvh265enc.c',
]

nvenc_option = get_option('nvenc')
if nvenc_option.disabled()
  subdir_done()
endif

use_nvenc_gl = false
extra_c_args = []

if host_machine.system() == 'windows'
  if cc.has_header('cuda_gl_interop.h', args: '-I' + cuda_incdir)
    use_nvenc_gl = true
  endif
else
  if cc.has_header('cuda_gl_interop.h', dependencies: cuda_dep)
    use_nvenc_gl = true
  endif
endif

if use_nvenc_gl
  nvenc_gl_dep = gstgl_dep
  extra_c_args += ['-DHAVE_NVENC_GST_GL=1']
endif

gstnvenc = library('gstnvenc',
  nvenc_sources,
  c_args : gst_plugins_bad_args + extra_c_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, nvenc_gl_dep, cuda_dep, cudart_dep, gmodule_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstnvenc, install_dir : plugins_pkgconfig_install_dir)

