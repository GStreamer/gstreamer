project('gst-libav', 'c',
  version : '1.23.0.1',
  meson_version : '>= 0.62',
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
gst_version_is_stable = gst_version_minor.is_even()
gst_version_is_dev = gst_version_minor.is_odd() and gst_version_micro < 90

api_version = '1.0'
libavfilter_dep = dependency('libavfilter', version: '>= 7.16.100',
  fallback: ['FFmpeg', 'libavfilter_dep'])
libavformat_dep = dependency('libavformat', version: '>= 58.12.100',
  fallback: ['FFmpeg', 'libavformat_dep'])
libavcodec_dep = dependency('libavcodec', version: '>= 58.18.100',
  fallback: ['FFmpeg', 'libavcodec_dep'])
libavutil_dep = dependency('libavutil', version: '>= 56.14.100',
  fallback: ['FFmpeg', 'libavutil_dep'])

libav_deps = [libavfilter_dep, libavformat_dep, libavcodec_dep, libavutil_dep]

cc = meson.get_compiler('c')
static_build = get_option('default_library') == 'static'

check_ffmpeg_src = '''#include <libavcodec/avcodec.h>
#if LIBAVCODEC_VERSION_MICRO >= 100
/* FFmpeg uses 100+ as its micro version */
#else
#error libav provider should be FFmpeg
#endif'''

libav_deps_type_name = ''

foreach dep: libav_deps
  if libav_deps_type_name != '' and dep.type_name() != libav_deps_type_name
    error('Libav deps must be either all internal or all external')
  endif
  libav_deps_type_name = dep.type_name()
endforeach

if dep.type_name() != 'internal'
  if not cc.compiles(check_ffmpeg_src, dependencies : libav_deps, name : 'libav is provided by FFmpeg')
    error('Incompatible libavcodec found')
  endif
endif

cdata = configuration_data()
cdata.set('LIBAV_SOURCE', '"system install"')
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE', '"gst-libav"')

# GStreamer package name and origin url
gst_package_name = get_option('package-name')
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
cdata.set_quoted('GST_PACKAGE_ORIGIN', get_option('package-origin'))


check_headers = [['unistd.h', 'HAVE_UNISTD_H']]

foreach h : check_headers
  if cc.has_header(h.get(0))
    cdata.set(h.get(1), 1)
  endif
endforeach

if gst_version_is_stable
  gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)
else
  gst_req = '>= ' + gst_version
endif

gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
  required : get_option('tests'),
  fallback : ['gstreamer', 'gst_check_dep'])

gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'audio_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
libm = cc.find_library('m', required : false)

gst_libav_args = ['-DHAVE_CONFIG_H']
if cc.get_id() == 'msvc'
  msvc_args = [
      # Ignore several spurious warnings for things gstreamer does very commonly
      # If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
      # If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
      # NOTE: Only add warnings here if you are sure they're spurious
      '/wd4018', # implicit signed/unsigned conversion
      '/wd4146', # unary minus on unsigned (beware INT_MIN)
      '/wd4244', # lossy type conversion (e.g. double -> int)
      '/wd4305', # truncating type conversion (e.g. double -> float)
      cc.get_supported_arguments(['/utf-8']), # set the input encoding to utf-8
  ]

  if gst_version_is_dev
    # Enable some warnings on MSVC to match GCC/Clang behaviour
    msvc_args += cc.get_supported_arguments([
      '/we4002', # too many actual parameters for macro 'identifier'
      '/we4003', # not enough actual parameters for macro 'identifier'
      '/we4013', # 'function' undefined; assuming extern returning int
      '/we4020', # 'function' : too many actual parameters
      '/we4027', # function declared without formal parameter list
      '/we4029', # declared formal parameter list different from definition
      '/we4033', # 'function' must return a value
      '/we4045', # 'array' : array bounds overflow
      '/we4047', # 'operator' : 'identifier1' differs in levels of indirection from 'identifier2'
      '/we4053', # one void operand for '?:'
      '/we4062', # enumerator 'identifier' in switch of enum 'enumeration' is not handled
      '/we4098', # 'function' : void function returning a value
    ])
  endif
  add_project_arguments(msvc_args, language: ['c', 'cpp'])
endif

# glib doesn't support unloading, which means that unloading and reloading
# any library that registers static types will fail
if cc.has_link_argument('-Wl,-z,nodelete')
  add_project_link_arguments('-Wl,-z,nodelete', language: 'c')
endif

# Symbol visibility
if cc.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'c')
endif

# Don't export any symbols from static ffmpeg libraries
if cc.has_link_argument('-Wl,--exclude-libs=ALL')
  add_project_link_arguments('-Wl,--exclude-libs=ALL', language: 'c')
endif

# Disable strict aliasing
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

# Disable compiler warnings for unused variables and args if gst debug system is disabled
if gst_dep.type_name() == 'internal'
  gst_proj = subproject('gstreamer')
  gst_debug_disabled = not gst_proj.get_variable('gst_debug')
else
  # We can't check that in the case of subprojects as we won't
  # be able to build against an internal dependency (which is not built yet)
  gst_debug_disabled = cc.has_header_symbol('gst/gstconfig.h', 'GST_DISABLE_GST_DEBUG', dependencies: gst_dep)
endif

if gst_debug_disabled
  message('GStreamer debug system is disabled')
  if cc.get_argument_syntax() == 'msvc'
    msvc_args = cc.get_supported_arguments([
      '/wd4101', # 'identifier' : unreferenced local variable
      '/wd4189', # 'identifier' : local variable is initialized but not referenced
    ])
    add_project_arguments(msvc_args, language: 'c')
  elif cc.has_argument('-Wno-unused')
    add_project_arguments(['-Wno-unused'], language: 'c')
  endif
else
  if cc.get_argument_syntax() == 'msvc' and gst_version_is_dev
    msvc_args = cc.get_supported_arguments([
      '/we4101', # 'identifier' : unreferenced local variable
      '/we4189', # 'identifier' : local variable is initialized but not referenced
    ])
    add_project_arguments(msvc_args, language: 'c')
  endif
  message('GStreamer debug system is enabled')
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

pkgconfig = import('pkgconfig')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

plugins = []
subdir('ext/libav')
subdir('docs')
subdir('tests')

# Set release date
if gst_version_nano == 0
  extract_release_date = find_program('scripts/extract-release-date-from-doap-file.py')
  run_result = run_command(extract_release_date, gst_version, files('gst-libav.doap'), check: true)
  release_date = run_result.stdout().strip()
  cdata.set_quoted('GST_PACKAGE_RELEASE_DATETIME', release_date)
  message('Package release date: ' + release_date)
endif

configure_file(output: 'config.h', configuration: cdata)

meson.add_dist_script('scripts/gen-changelog.py', meson.project_name(), '1.20.0', meson.project_version())

gst_plugins = []
foreach plugin: plugins
  pkgconfig.generate(plugin, install_dir: plugins_pkgconfig_install_dir)
  dep = declare_dependency(link_with: plugin, variables: {'full_path': plugin.full_path()})
  meson.override_dependency(plugin.name(), dep)
  gst_plugins += [dep]
endforeach
