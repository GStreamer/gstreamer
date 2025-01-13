nvdswrapper_sources = [
  'plugin.cpp',
  'gstnvdsdewarp.cpp',
]

nvdswrapper_headers = [
  'gstnvdsdewarp.h',
]

doc_sources = []
foreach s: nvdswrapper_sources + nvdswrapper_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'nvdswrapper': pathsep.join(doc_sources)
}

extra_args = ['-DGST_USE_UNSTABLE_API']

nvdswrapper_opt = get_option('nvdswrapper')
if nvdswrapper_opt.disabled() or host_system != 'linux'
  subdir_done()
endif

if not gstcuda_dep.found()
  if nvdswrapper_opt.enabled()
    error('nvdswrapper plugin was enabled explicitly, but required gstcuda was not found')
  endif
  subdir_done()
endif

have_sdk_header = false
nvds_inc = []
nvds_dewarper_lib = dependency('', required : false)

nvds_include_path = get_option('nvds-include-path')
if nvds_include_path != ''
  nvds_inc = [include_directories(nvds_include_path)]
  if cc.has_header('NVWarp360.h', include_directories: nvds_inc)
    have_sdk_header = true
  endif
endif

if not have_sdk_header
  if cc.has_header('/opt/nvidia/deepstream/deepstream/sources/includes/NVWarp360.h')
    nvds_inc = [include_directories('/opt/nvidia/deepstream/deepstream/sources/includes')]
  else
    if nvdswrapper_opt.enabled()
      error('Could not find required header: "NVWarp360.h"')
    endif
    subdir_done()
  endif
endif

nvds_lib_path = get_option('nvds-lib-path')
if nvds_lib_path != ''
  nvds_dewarper_lib = cc.find_library('libnvds_dewarper.so', dirs: nvds_lib_path, required: false)
endif

if not nvds_dewarper_lib.found()
  nvds_lib_path = '/opt/nvidia/deepstream/deepstream/lib'
  nvds_dewarper_lib = cc.find_library('nvds_dewarper', dirs: nvds_lib_path, required: false)
  if not nvds_dewarper_lib.found()
    if nvdswrapper_opt.enabled()
      error('Could not find required library: "nvds_dewarper"')
    endif
    subdir_done()
  endif
endif

nvds_inc += [include_directories('stub'), cuda_stubinc]

gstnvdswrapper = library('gstnvdswrapper',
  nvdswrapper_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args : gst_plugins_bad_args + extra_args,
  include_directories : [configinc] + nvds_inc,
  dependencies :  [gstbase_dep, gstvideo_dep, gstcuda_dep, nvds_dewarper_lib],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstnvdswrapper]
