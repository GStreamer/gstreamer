nvcomp_sources = [
  'gstnvcomp.cpp',
  'gstnvcompvideodec.cpp',
  'gstnvcompvideoenc.cpp',
  'plugin.cpp',
]
extra_args = ['-DGST_USE_UNSTABLE_API']

nvcomp_opt = get_option('nvcomp')
if nvcomp_opt.disabled() or host_system not in ['windows', 'linux']
  subdir_done()
endif

nvcomp_sdk_path = get_option('nvcomp-sdk-path')
if nvcomp_sdk_path == ''
  nvcomp_sdk_path = run_command(python3, '-c', 'import os; print(os.environ.get("NVCOMP_SDK_PATH"))', check: false).stdout().strip()
endif

if nvcomp_sdk_path == '' or nvcomp_sdk_path == 'None'
  if nvcomp_opt.enabled()
    error('nvcomp-sdk-path option must be specified for nvCOMP plugin')
  endif
  subdir_done()
endif

if not gstcuda_dep.found()
  if nvcomp_opt.enabled()
    error('nvCOMP plugin was enabled explicitly, but required gstcuda was not found')
  endif
  subdir_done()
endif

nvcomp_inc_dirs = [include_directories(join_paths(nvcomp_sdk_path, 'include'), './stub'),
                   cuda_stubinc]

nvcomp_lib_path = join_paths(nvcomp_sdk_path, 'lib')
nvcomp_lib = cc.find_library('nvcomp',
                             dirs: nvcomp_lib_path, required: nvcomp_opt)
if not nvcomp_lib.found()
  subdir_done()
endif

nvcomp_bitcomp_lib = cc.find_library('nvcomp_bitcomp',
                                     dirs: nvcomp_lib_path, required: nvcomp_opt)
if not nvcomp_bitcomp_lib.found()
  subdir_done()
endif

nvcomp_gdeflate_lib = cc.find_library('nvcomp_gdeflate',
                                      dirs: nvcomp_lib_path, required: nvcomp_opt)
if not nvcomp_gdeflate_lib.found()
  subdir_done()
endif

if gstgl_dep.found()
  extra_args += ['-DHAVE_GST_GL']
endif

gstnvcomp = library('gstnvcomp',
  nvcomp_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args : gst_plugins_bad_args + extra_args,
  include_directories : [configinc] + nvcomp_inc_dirs,
  dependencies :  [gstbase_dep, gstvideo_dep, gstcuda_dep, gstgl_dep, nvcomp_lib,
                  nvcomp_bitcomp_lib, nvcomp_gdeflate_lib],
  override_options : ['cpp_std=c++17'],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstnvcomp]
