project('gst-aja', 'cpp',
  version : '0.1.0',
  meson_version : '>= 0.63.0',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized',
                      'cpp_std=c++11',
                      'cpp_eh=none',
                      'cpp_rtti=false',
                    ]
)

plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

cxx = meson.get_compiler('cpp')

if cxx.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'cpp')
endif

if cxx.get_id() == 'msvc'
  # Ignore several spurious warnings for things gstreamer does very commonly
  # If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
  # If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
  # NOTE: Only add warnings here if you are sure they're spurious
  test_cppflags = []
  msvc_args = [
      '/wd4018', # implicit signed/unsigned conversion
      '/wd4146', # unary minus on unsigned (beware INT_MIN)
      '/wd4244', # lossy type conversion (e.g. double -> int)
      '/wd4305', # truncating type conversion (e.g. double -> float)
  ]
  add_project_arguments(msvc_args, language : 'cpp')
  # Disable SAFESEH with MSVC for plugins and libs that use external deps that
  # are built with MinGW
  noseh_link_args = ['/SAFESEH:NO']
else
  test_cppflags = ['-Wno-non-virtual-dtor']
  noseh_link_args = []
endif

common_flags = [
  '-DAJALinux=1',
  '-DAJA_LINUX=1',
]
foreach cxxflag: test_cppflags
  if cxx.has_argument(cxxflag)
    common_flags += [ cxxflag ]
  endif
endforeach

gst_dep = dependency('gstreamer-1.0', version : '>= 1.18', required : true)
gstbase_dep = dependency('gstreamer-base-1.0', version : '>= 1.18', required : true)
gstaudio_dep = dependency('gstreamer-audio-1.0', version : '>= 1.18', required : true)
gstvideo_dep = dependency('gstreamer-video-1.0', version : '>= 1.18', required : true)

thread_dep = dependency('threads')
rt_dep = cxx.find_library('rt', required : false)

aja_sdk_dir = get_option('aja-sdk-dir')
if aja_sdk_dir == ''
  ajantv2_dep = dependency('libajantv2')
  aja_includedirs = []

  if not ajantv2_dep.found()
    subdir_done()
  endif
else
  aja_includedirs = include_directories(
    f'@aja_sdk_dir@/ajalibraries',
    f'@aja_sdk_dir@/ajalibraries/ajantv2/includes',
    f'@aja_sdk_dir@/ajalibraries/ajantv2/src/lin',
  )

  message('Looking for AJA SDK in directory ' + aja_sdk_dir)
  if not cxx.has_header('ajabase/common/videotypes.h',
                        include_directories : aja_includedirs,
                       )
    error('Cannot find AJA SDK')
  endif


  ajantv2_lib = cxx.find_library('ajantv2',
    # If the header is found, this should also be
    required : true,
    dirs : [f'@aja_sdk_dir@/lib'],
  )
  ajantv2_dep = declare_dependency(
    dependencies: ajantv2_lib,
    include_directories: aja_includedirs,
  )
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
  cpp_args : [
            '-DPACKAGE="gst-aja"',
            '-DGST_PACKAGE_NAME="gstreamer-aja"',
            '-DGST_PACKAGE_ORIGIN="https://github.com/centricular/gstreamer-aja"',
            '-DVERSION="@0@"'.format(meson.project_version())] + common_flags,
  link_args : noseh_link_args,
  dependencies : [gstvideo_dep, gstaudio_dep, gstbase_dep, gst_dep, ajantv2_dep, thread_dep, rt_dep],
  install : true,
  install_dir : plugins_install_dir,
)

