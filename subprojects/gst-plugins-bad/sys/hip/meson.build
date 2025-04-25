hip_sources = [
  'gsthipbasefilter.cpp',
  'gsthipbufferpool.cpp',
  'gsthipconverter.cpp',
  'gsthipconvertscale.cpp',
  'gsthipdevice.cpp',
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
  '-D__HIP_PLATFORM_AMD__',
  '-D__HIP_DISABLE_CPP_FUNCTIONS__',
]

hip_option = get_option('hip')
if hip_option.disabled()
  subdir_done()
endif

if host_system not in ['linux', 'windows']
  subdir_done()
endif

hip_dep = dependency('', required: false)
hip_root = run_command(python3, '-c', 'import os; print(os.environ.get("HIP_PATH"))', check: false).stdout().strip()

if host_system == 'windows'
  hip_root = run_command(python3, '-c', 'import os; print(os.environ.get("HIP_PATH"))', check: false).stdout().strip()
else
  # HIP does not offer pkg-config it seems
  hip_root = '/opt/rocm'
endif

if hip_root != '' and hip_root != 'None'
  hip_lib_dir = join_paths(hip_root, 'lib')
  hip_lib = cc.find_library('amdhip64', dirs: hip_lib_dir,
                              required : hip_option)
  hiprtc_lib = cc.find_library('hiprtc', dirs: hip_lib_dir,
                                required : hip_option)

  hip_inc_dir = include_directories(join_paths(hip_root, 'include'))
  has_hip_header = cc.has_header('hip/hip_runtime.h',
                                  include_directories: hip_inc_dir,
                                  required: hip_option)

  has_hiprtc_header = cc.has_header('hip/hiprtc.h',
                                    include_directories: hip_inc_dir,
                                    required: hip_option)

  if hip_lib.found() and hiprtc_lib.found() and has_hip_header and has_hiprtc_header
    hip_dep = declare_dependency(include_directories: hip_inc_dir,
                                  dependencies: [hip_lib, hiprtc_lib])
  endif
endif

if not hip_dep.found()
  if hip_option.enabled()
    error('The hip was enabled explicitly, but required dependencies were not found')
  endif
  subdir_done()
endif

extra_args += cc.get_supported_arguments([
  '-Wno-undef'
])

gsthip = library('gsthip', hip_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args: gst_plugins_bad_args + extra_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, hip_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gsthip]
