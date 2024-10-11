aja_option = get_option('aja') \
    .require(host_system == 'linux',
             error_message: 'AJA plugin is currently only available on Linux')
if aja_option.disabled()
  subdir_done()
endif

common_flags = []

thread_dep = dependency('threads')
rt_dep = cxx.find_library('rt', required : false)
dl_dep = cxx.find_library('dl', required : false)

ajantv2_args = [
  '-DAJALinux',
  '-DAJASTATIC',
  '-DAJA_LINUX',
  '-DAJA_USE_CPLUSPLUS11',
  '-DNDEBUG',
  '-DNTV2_USE_CPLUSPLUS11',
]

aja_include_dir = get_option('aja-include-dir')
aja_lib_dir = get_option('aja-lib-dir')

# Try to automatically find the AJA NTV2 SDK and otherwise fall back to the subproject
found_ajantv2 = false
found_ajantv2_includes = false

if aja_include_dir != ''
  aja_include_dirs = [aja_include_dir]
elif not meson.is_cross_build()
  aja_include_dirs = ['/usr/include', '/usr/local/include']
else
  message('Cross-compiling: cannot autodetect AJA, please set the options "aja-include-dir" and "aja-lib-dir"')
  aja_include_dirs = []
endif

foreach dir : aja_include_dirs
  message(f'Checking for AJA NTV2 headers in @dir@')

  fs = import('fs')
  dir = f'@dir@/libajantv2'
  if not fs.is_dir(dir)
    continue
  endif

  include_dir = include_directories([
                                     f'@dir@',
                                     f'@dir@/ajaanc/includes',
                                     f'@dir@/ajantv2/includes',
                                     f'@dir@/ajantv2/src/lin',
                                    ],
                                    is_system : true)

  if cxx.check_header('ajantv2/includes/ntv2card.h',
                      args : ajantv2_args,
                      include_directories : include_dir)
    ajantv2_include_dir = include_dir
    found_ajantv2_includes = true
    break
  endif
endforeach

# If provided explicitly then consider it an error to not find the headers
if aja_include_dir != '' and not found_ajantv2_includes
  error(f'Did not found AJA NTV2 headers in @aja_include_dir@')
endif

# If we found the includes, now check for the library
if found_ajantv2_includes
  if aja_lib_dir == ''
    ajantv2_lib = cxx.find_library('ajantv2',
      # If the header is found, this should also be
      required : false)
  else
    ajantv2_lib = cxx.find_library('ajantv2',
      dirs : [aja_lib_dir],
      required : true)
  endif

  if ajantv2_lib.found()
    found_ajantv2 = true

    libajantv2_dep = declare_dependency(
        compile_args : ajantv2_args,
        dependencies: ajantv2_lib,
        include_directories: ajantv2_include_dir)
  endif
endif

# If the includes or the library or not found, fall back to the subproject
if not found_ajantv2
  libajantv2_dep = dependency('libajantv2',
                              include_type: 'system',
                              required: aja_option,
                              allow_fallback: true,
                              default_options: ['warning_level=0'])
  if not libajantv2_dep.found()
    subdir_done()
  endif
endif

gstaja = library('gstaja',
  ['plugin.cpp',
   'gstajacommon.cpp',
   'gstajasink.cpp',
   'gstajasinkcombiner.cpp',
   'gstajasrc.cpp',
   'gstajasrcdemux.cpp',
   'gstajadeviceprovider.cpp',
  ],
  override_options : ['cpp_std=c++11'],
  cpp_args : gst_plugins_bad_args + common_flags,
  include_directories : [configinc],
  link_args : noseh_link_args,
  dependencies : [gstvideo_dep, gstaudio_dep, gstbase_dep, gst_dep, libajantv2_dep, thread_dep, rt_dep, dl_dep],
  install : true,
  install_dir : plugins_install_dir,
)

plugins += [gstaja]
