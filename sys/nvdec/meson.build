nvdec_sources = [
  'gstnvdec.c',
  'plugin.c'
]

nvdec_option = get_option('nvdec')
if nvdec_option.disabled()
  subdir_done()
endif

nvcuvid_dep_found = false
if host_machine.system() == 'windows'
  nvcuvid_lib = cc.find_library('nvcuvid', dirs: cuda_libdir, required: nvdec_option)
else
  nvcuvid_lib = cc.find_library('nvcuvid', required: nvdec_option)
endif

if nvcuvid_lib.found() and cc.has_function('cuvidCtxLock', dependencies: nvcuvid_lib)
  nvcuvid_dep = declare_dependency(dependencies: nvcuvid_lib)
  nvcuvid_dep_found = true
endif

if nvdec_option.enabled() and not nvcuvid_dep_found
  error('The nvdec plugin was enabled explicitly, but required nvcuvid library was not found.')
endif

if nvcuvid_dep_found
  gstnvdec = library('gstnvdec',
    nvdec_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, gstpbutils_dep, gstgl_dep, cuda_dep, cudart_dep, nvcuvid_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstnvdec, install_dir : plugins_pkgconfig_install_dir)
endif

