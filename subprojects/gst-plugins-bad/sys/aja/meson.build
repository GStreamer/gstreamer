aja_sources = [
  'plugin.cpp',
  'gstajacommon.cpp',
  'gstajasink.cpp',
  'gstajasinkcombiner.cpp',
  'gstajasrc.cpp',
  'gstajasrcdemux.cpp',
  'gstajadeviceprovider.cpp',
]

aja_headers = [
  'gstajacommon.h',
  'gstajasinkcombiner.h',
  'gstajasink.h',
  'gstajasrcdemux.h',
  'gstajadeviceprovider.h',
  'gstajasrc.h',
]

doc_sources = []
foreach s: aja_sources + aja_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'aja': pathsep.join(doc_sources)
}

aja_option = get_option('aja').require(host_system == 'linux' or subsystem == 'macos' or (host_system == 'windows' and cc.get_define('_MSC_VER') != ''),
             error_message: 'AJA plugin is currently only available on Lin/macOS or Windows with MSVC')
if aja_option.disabled()
  subdir_done()
endif

# Option #1: CMake include (supported from 17.1.0 onwards)
libajantv2_dep = dependency(
  'ajantv2',
  method: 'cmake',
  modules: ['AJA::ajantv2'],
  include_type: 'system',
  # Do not allow fallback just yet
  allow_fallback: false,
  required: false,
)

found_ajantv2 = libajantv2_dep.found()

# Option #2: Try the Cerbero wrap or fall back to the subproject
libajantv2_dep = dependency('libajantv2',
                            include_type: 'system',
                            allow_fallback: true,
                            required: false,
                            default_options: ['warning_level=0'])

found_ajantv2 = libajantv2_dep.found()

# Option 3: Try to put together a synthetic dependency (find lib and headers)
if not found_ajantv2
  ajantv2_deps = [
    dependency('threads'),
  ]

  if host_machine.system() == 'linux'
    ajantv2_deps += [
      cxx.find_library('rt', required : false),
      cxx.find_library('dl', required : false),
      dependency('libudev', required : false),
    ]
  elif host_machine.system() == 'darwin'
    ajantv2_deps += [
      dependency('appleframeworks', modules : ['Foundation', 'CoreFoundation', 'IOKit']),
    ]
  elif host_machine.system() == 'windows'
    ajantv2_deps += [
      cxx.find_library('advapi32'),
      cxx.find_library('comctl32'),
      cxx.find_library('netapi32'),
      cxx.find_library('odbc32'),
      cxx.find_library('psapi'),
      cxx.find_library('rpcrt4'),
      cxx.find_library('setupapi'),
      cxx.find_library('shell32'),
      cxx.find_library('shlwapi'),
      cxx.find_library('user32'),
      cxx.find_library('wbemuuid'),
      cxx.find_library('winmm'),
      cxx.find_library('ws2_32'),
      cxx.find_library('wsock32'),
      cxx.find_library('bcrypt'),
    ]
  endif

  ajantv2_args = [
    '-DAJASTATIC',
    '-DAJA_USE_CPLUSPLUS11',
    '-DNDEBUG',
    '-DNTV2_USE_CPLUSPLUS11',
  ]

  if host_machine.system() == 'linux'
    ajantv2_args += [
      '-DAJALinux',
      '-DAJA_LINUX',
    ]
  elif host_machine.system() == 'darwin'
    ajantv2_args += [
      '-DAJAMac',
      '-DAJA_MAC',
      '-D__STDC_CONSTANT_MACROS',
    ]
  elif host_machine.system() == 'windows'
    ajantv2_args += [
      '-DAJA_WINDOWS',
      '-DMSWindows',
      '-D_WINDOWS',
      '-D_CONSOLE',
      '-DWIN32_LEAN_AND_MEAN',
      '-D_CRT_SECURE_NO_WARNINGS',
      '-D_SCL_SECURE_NO_WARNINGS',
    ]
  endif

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
                                      f'@dir@/ajantv2/src/mac',
                                      f'@dir@/ajantv2/src/win',
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
          dependencies: [ajantv2_lib, ajantv2_deps],
          include_directories: ajantv2_include_dir)
    endif
  endif
endif

if not found_ajantv2
  subdir_done()
endif

gstaja = library('gstaja',
  aja_sources,
  override_options : ['cpp_std=c++11'],
  cpp_args : gst_plugins_bad_args,
  include_directories : [configinc],
  link_args : noseh_link_args,
  dependencies : [gstvideo_dep, gstaudio_dep, gstbase_dep, gst_dep, libajantv2_dep],
  install : true,
  install_dir : plugins_install_dir,
)

plugins += [gstaja]
