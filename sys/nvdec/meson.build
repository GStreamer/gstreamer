nvdec_sources = [
  'gstnvdec.c',
  'plugin.c'
]

nvcuvid_dep_found = false
nvcuvid_incdirs = []

nvdec_option = get_option('nvdec')
if nvdec_option.disabled()
  subdir_done()
endif

if host_machine.system() == 'windows'
  cuda_libdirs = [cuda_libdir]
  # NOTE: Newer CUDA toolkit versions do not ship with the nvcuvid library, and
  # you must get it from the Nvidia Video Codec SDK. The SDK ships as a zip
  # file, so there's no installer and you have to set this env var yourself.
  video_sdk_root = run_command(python3, '-c', 'import os; print(os.environ.get("NVIDIA_VIDEO_CODEC_SDK_PATH"))').stdout().strip()
  if video_sdk_root != '' and video_sdk_root != 'None'
    cuda_libdirs += [join_paths(video_sdk_root, 'Samples', 'NvCodec', 'Lib', arc)]
    nvcuvid_incdirs = include_directories(join_paths(video_sdk_root, 'Samples', 'NvCodec', 'NvDecoder'))
  endif
  nvcuvid_lib = cc.find_library('nvcuvid', dirs: cuda_libdirs, required: nvdec_option)
else
  nvcuvid_lib = cc.find_library('nvcuvid', required: nvdec_option)
endif

if nvcuvid_lib.found() and cc.has_function('cuvidCtxLock', dependencies: nvcuvid_lib)
  nvcuvid_dep = declare_dependency(dependencies: nvcuvid_lib,
                                   include_directories: nvcuvid_incdirs)
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

