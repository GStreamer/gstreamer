project('gst-libav', 'c', 'cpp',
  version : '1.14.0',
  meson_version : '>= 0.36.0',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0].to_int()
gst_version_minor = version_arr[1].to_int()
gst_version_micro = version_arr[2].to_int()
 if version_arr.length() == 4
  gst_version_nano = version_arr[3].to_int()
else
  gst_version_nano = 0
endif

libavfilter_dep = dependency('libavfilter', version: '>= 6.47.100')
libavformat_dep = dependency('libavformat', version: '>= 57.41.100')
libavcodec_dep = dependency('libavcodec', version: '>= 57.48.101')
libavutil_dep = dependency('libavutil', version: '>= 55.28.100')

libav_deps = [libavfilter_dep, libavformat_dep, libavcodec_dep, libavutil_dep]

cc = meson.get_compiler('c')

check_ffmpeg_src = '''#include <libavcodec/avcodec.h>
#if LIBAVCODEC_VERSION_MICRO >= 100
/* FFmpeg uses 100+ as its micro version */
#else
#error libav provider should be FFmpeg
#endif'''

if not cc.compiles(check_ffmpeg_src, dependencies : libav_deps, name : 'libav is provided by FFmpeg')
    error('Uncompatible libavcodec found')
endif

cdata = configuration_data()
cdata.set('LIBAV_SOURCE', '"system install"')
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE', '"gst-libav"')

# GStreamer package name and origin url
gst_package_name = get_option('with-package-name')
if gst_package_name == ''
  if gst_version_nano == 0
    gst_package_name = 'GStreamer FFMPEG Plug-ins source release'
  elif gst_version_nano == 1
    gst_package_name = 'GStreamer FFMPEG Plug-ins git'
  else
    gst_package_name = 'GStreamer FFMPEG Plug-ins prerelease'
  endif
endif
cdata.set_quoted('GST_PACKAGE_NAME', gst_package_name)
cdata.set_quoted('GST_PACKAGE_ORIGIN', get_option('with-package-origin'))


check_headers = [['unistd.h', 'HAVE_UNISTD_H']]

foreach h : check_headers
  if cc.has_header(h.get(0))
    cdata.set(h.get(1), 1)
  endif
endforeach

gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])

gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'audio_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
libm = cc.find_library('m', required : false)

configure_file(output : 'config.h', configuration : cdata)

gst_libav_args = ['-DHAVE_CONFIG_H']
if cc.get_id() != 'msvc'
  gst_libav_args += ['-Wno-deprecated-declarations']
else
  # Ignore several spurious warnings for things gstreamer does very commonly
  # If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
  # If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
  # NOTE: Only add warnings here if you are sure they're spurious
  add_project_arguments(
      '/wd4018', # implicit signed/unsigned conversion
      '/wd4146', # unary minus on unsigned (beware INT_MIN)
      '/wd4244', # lossy type conversion (e.g. double -> int)
      '/wd4305', # truncating type conversion (e.g. double -> float)
      language : 'c')
endif

# Symbol visibility
if cc.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'c')
endif

# Disable strict aliasing
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

if gst_dep.type_name() == 'internal'
    gst_proj = subproject('gstreamer')

    if gst_proj.get_variable('disable_gst_debug')
        message('GStreamer debug system is disabled')
        add_project_arguments('-Wno-unused', language: 'c')
    else
        message('GStreamer debug system is enabled')
    endif
else
    # We can't check that in the case of subprojects as we won't
    # be able to build against an internal dependency (which is not built yet)
    if not cc.compiles('''
#include <gst/gstconfig.h>
#ifdef GST_DISABLE_GST_DEBUG
#error "debugging disabled, make compiler fail"
#endif''' , dependencies: gst_dep)
        message('GStreamer debug system is disabled')
        add_project_arguments('-Wno-unused', language: 'c')
    else
        message('GStreamer debug system is enabled')
    endif
endif

warning_flags = [
  '-Wmissing-declarations',
  '-Wmissing-prototypes',
  '-Wold-style-definition',
  '-Wredundant-decls',
  '-Wundef',
  '-Wwrite-strings',
  '-Wformat',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Winit-self',
  '-Wmissing-include-dirs',
  '-Waddress',
  '-Wno-multichar',
  '-Waggregate-return',
  '-Wdeclaration-after-statement',
  '-Wvla',
  '-Wpointer-arith',
]

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

configinc = include_directories('.')
plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))
subdir('ext/libav/')

python3 = import('python3').find_python()
run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
