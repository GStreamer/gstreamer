project('gst-plugins-ugly', 'c',
  version : '1.17.90',
  meson_version : '>= 0.48',
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
gst_version_is_dev = gst_version_minor % 2 == 1 and gst_version_micro < 90

have_cxx = add_languages('cpp', native: false, required: false)

glib_req = '>= 2.44.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

api_version = '1.0'

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins = []

cc = meson.get_compiler('c')
if have_cxx
  cxx = meson.get_compiler('cpp')
endif

if cc.get_id() == 'msvc'
  # Ignore several spurious warnings for things gstreamer does very commonly
  # If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
  # If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
  # NOTE: Only add warnings here if you are sure they're spurious
  add_project_arguments(
      '/wd4018', # implicit signed/unsigned conversion
      '/wd4146', # unary minus on unsigned (beware INT_MIN)
      '/wd4244', # lossy type conversion (e.g. double -> int)
      '/wd4305', # truncating type conversion (e.g. double -> float)
      cc.get_supported_arguments(['/utf-8']), # set the input encoding to utf-8
      language : 'c')
  # Disable SAFESEH with MSVC for plugins and libs that use external deps that
  # are built with MinGW
  noseh_link_args = ['/SAFESEH:NO']
else
  noseh_link_args = []
endif

if cc.has_link_argument('-Wl,-Bsymbolic-functions')
  add_project_link_arguments('-Wl,-Bsymbolic-functions', language : 'c')
endif
if have_cxx and cxx.has_link_argument('-Wl,-Bsymbolic-functions')
  add_project_link_arguments('-Wl,-Bsymbolic-functions', language : 'cpp')
endif

cdata = configuration_data()
check_headers = [
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_MALLOC_H', 'malloc.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_UNISTD_H', 'unistd.h'],
  ['HAVE_WINSOCK2_H', 'winsock2.h'],
]

foreach h : check_headers
  if cc.has_header(h.get(1))
    cdata.set(h.get(0), 1)
  endif
endforeach

check_functions = [
  ['HAVE_DCGETTEXT', 'dcgettext'], # FIXME: this looks unused
]

foreach f : check_functions
  if cc.has_function(f.get(1))
    cdata.set(f.get(0), 1)
  endif
endforeach

cdata.set('SIZEOF_CHAR', cc.sizeof('char'))
cdata.set('SIZEOF_INT', cc.sizeof('int'))
cdata.set('SIZEOF_LONG', cc.sizeof('long'))
cdata.set('SIZEOF_SHORT', cc.sizeof('short'))
cdata.set('SIZEOF_VOIDP', cc.sizeof('void*'))

cdata.set_quoted('VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-plugins-ugly')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GETTEXT_PACKAGE', 'gst-plugins-ugly-1.0')
cdata.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))

# GStreamer package name and origin url
gst_package_name = get_option('package-name')
if gst_package_name == ''
  if gst_version_nano == 0
    gst_package_name = 'GStreamer Ugly Plug-ins source release'
  elif gst_version_nano == 1
    gst_package_name = 'GStreamer Ugly Plug-ins git'
  else
    gst_package_name = 'GStreamer Ugly Plug-ins prerelease'
  endif
endif
cdata.set_quoted('GST_PACKAGE_NAME', gst_package_name)
cdata.set_quoted('GST_PACKAGE_ORIGIN', get_option('package-origin'))

# Mandatory GST deps
gst_dep = dependency('gstreamer-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_dep'])
gstapp_dep = dependency('gstreamer-app-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'app_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gsttag_dep = dependency('gstreamer-tag-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'tag_dep'])
gstfft_dep = dependency('gstreamer-fft-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'fft_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'audio_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstriff_dep = dependency('gstreamer-riff-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'riff_dep'])
gstrtp_dep = dependency('gstreamer-rtp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'rtp_dep'])
gstnet_dep = dependency('gstreamer-net-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_net_dep'])
gstsdp_dep = dependency('gstreamer-sdp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'sdp_dep'])
gstrtsp_dep = dependency('gstreamer-rtsp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'rtsp_dep'])
gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
    required : get_option('tests'),
    fallback : ['gstreamer', 'gst_check_dep'])
gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])

orc_dep = dependency('orc-0.4', version : '>= 0.4.16', required : get_option('orc'),
    fallback : ['orc', 'orc_dep'])
if orc_dep.found()
  cdata.set('HAVE_ORC', 1) # used by a52dec for cpu detection
else
  cdata.set('DISABLE_ORC', 1)
endif

gmodule_dep = dependency('gmodule-2.0', fallback : ['glib', 'libgmodule_dep'])

ugly_args = ['-DHAVE_CONFIG_H']
configinc = include_directories('.')
libsinc = include_directories('gst-libs')

# Disable compiler warnings for unused variables and args if gst debug system is disabled
if gst_dep.type_name() == 'internal'
  gst_debug_disabled = not subproject('gstreamer').get_variable('gst_debug')
else
  # We can't check that in the case of subprojects as we won't
  # be able to build against an internal dependency (which is not built yet)
  gst_debug_disabled = cc.has_header_symbol('gst/gstconfig.h', 'GST_DISABLE_GST_DEBUG', dependencies: gst_dep)
endif

if gst_debug_disabled
  message('GStreamer debug system is disabled')
  if cc.has_argument('-Wno-unused')
    add_project_arguments('-Wno-unused', language: 'c')
  endif
  if have_cxx and cxx.has_argument ('-Wno-unused')
    add_project_arguments('-Wno-unused', language: 'cpp')
  endif
else
  message('GStreamer debug system is enabled')
endif

warning_flags = [
  '-Wmissing-declarations',
  '-Wredundant-decls',
  '-Wwrite-strings',
  '-Wformat',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Winit-self',
  '-Wmissing-include-dirs',
  '-Waddress',
  '-Wno-multichar',
  '-Wvla',
  '-Wpointer-arith',
  '-Waggregate-return',
  '-fno-strict-aliasing',
  # Symbol visibility
  '-fvisibility=hidden',
]

warning_c_flags = [
  '-Wmissing-prototypes',
  '-Wold-style-definition',
  '-Wdeclaration-after-statement',
  '-Wnested-externs'
]

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
  if have_cxx and cxx.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'cpp')
  endif
endforeach

foreach extra_arg : warning_c_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

# Define G_DISABLE_DEPRECATED for development versions
if gst_version_is_dev
  message('Disabling deprecated GLib API')
  add_project_arguments('-DG_DISABLE_DEPRECATED', language: 'c')
endif

cast_checks = get_option('gobject-cast-checks')
if cast_checks.disabled() or (cast_checks.auto() and not gst_version_is_dev)
  message('Disabling GLib cast checks')
  add_project_arguments('-DG_DISABLE_CAST_CHECKS', language: 'c')
endif

glib_asserts = get_option('glib-asserts')
if glib_asserts.disabled() or (glib_asserts.auto() and not gst_version_is_dev)
  message('Disabling GLib asserts')
  add_project_arguments('-DG_DISABLE_ASSERT', language: 'c')
endif

glib_checks = get_option('glib-checks')
if glib_checks.disabled() or (glib_checks.auto() and not gst_version_is_dev)
  message('Disabling GLib checks')
  add_project_arguments('-DG_DISABLE_CHECKS', language: 'c')
endif

presetdir = join_paths(get_option('datadir'), 'gstreamer-' + api_version, 'presets')

pkgconfig = import('pkgconfig')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

python3 = import('python').find_installation()
subdir('gst')
subdir('ext')
subdir('tests')

# xgettext is optional (on Windows for instance)
if find_program('xgettext', required : get_option('nls')).found()
  cdata.set('ENABLE_NLS', 1)
  subdir('po')
endif
subdir('docs')

# Set release date
if gst_version_nano == 0
  extract_release_date = find_program('scripts/extract-release-date-from-doap-file.py')
  run_result = run_command(extract_release_date, gst_version, files('gst-plugins-ugly.doap'))
  if run_result.returncode() == 0
    release_date = run_result.stdout().strip()
    cdata.set_quoted('GST_PACKAGE_RELEASE_DATETIME', release_date)
    message('Package release date: ' + release_date)
  else
    # Error out if our release can't be found in the .doap file
    error(run_result.stderr())
  endif
endif

configure_file(output : 'config.h', configuration : cdata)

run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')

if meson.version().version_compare('>= 0.54')
  plugin_names = []
  foreach plugin: plugins
    # FIXME: Use str.subtring() when we can depend on Meson 0.56
    split = plugin.name().split('gst')
    if split.length() == 2
      plugin_names += [split[1]]
    else
      warning('Need substring API in meson >= 0.56 to properly parse plugin name: ' + plugin.name())
      plugin_names += [plugin.name()]
    endif
  endforeach
  summary({'Plugins':plugin_names}, list_sep: ', ')
endif
