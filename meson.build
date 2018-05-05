project('gst-rtsp-server', 'c',
  version : '1.15.0.1',
  meson_version : '>= 0.46.0',
  default_options : ['warning_level=1', 'buildtype=debugoptimized'])

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

glib_req = '>= 2.40.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

api_version = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
libversion = '@0@.@1@.0'.format(soversion, gst_version_minor * 100 + gst_version_micro)

plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

cc = meson.get_compiler('c')

if cc.has_link_argument('-Wl,-Bsymbolic-functions')
  add_project_link_arguments('-Wl,-Bsymbolic-functions', language : 'c')
endif

# Symbol visibility
if cc.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'c')
endif

# Disable strict aliasing
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

cdata = configuration_data()
cdata.set_quoted('GETTEXT_PACKAGE', 'gst-rtsp-server-1.0')
cdata.set_quoted('PACKAGE', 'gst-rtsp-server')
cdata.set_quoted('VERSION', gst_version)
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_LICENSE', 'LGPL')

# FIXME: ENABLE_NLS (currently also missing from autotools build)
# cdata.set('ENABLE_NLS', true)
# cdata.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))

# GStreamer package name and origin url
gst_package_name = get_option('package-name')
if gst_package_name == ''
  if gst_version_nano == 0
    gst_package_name = 'GStreamer RTSP Server Library source release'
  elif gst_version_nano == 1
    gst_package_name = 'GStreamer RTSP Server Library git'
  else
    gst_package_name = 'GStreamer RTSP Server Library prerelease'
  endif
endif
cdata.set_quoted('GST_PACKAGE_NAME', gst_package_name)
cdata.set_quoted('GST_PACKAGE_ORIGIN', get_option('package-origin'))

configure_file(output : 'config.h', configuration : cdata)

rtspserver_args = ['-DHAVE_CONFIG_H']

warning_flags = [
  '-Wmissing-declarations',
  '-Wmissing-prototypes',
  '-Wredundant-decls',
  '-Wundef',
  '-Wwrite-strings',
  '-Wformat',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wold-style-definition',
  '-Waggregate-return',
  '-Wnested-externs',
  '-Winit-self',
  '-Wmissing-include-dirs',
  '-Waddress',
  '-Wno-multichar',
  '-Wdeclaration-after-statement',
  '-Wvla',
  '-Wpointer-arith',
]

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

rtspserver_incs = include_directories('gst/rtsp-server', '.')

glib_dep = dependency('glib-2.0', version : glib_req,
  fallback: ['glib', 'libglib_dep'])
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstrtsp_dep = dependency('gstreamer-rtsp-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'rtsp_dep'])
gstrtp_dep = dependency('gstreamer-rtp-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'rtp_dep'])
gstsdp_dep = dependency('gstreamer-sdp-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'sdp_dep'])
gstapp_dep = dependency('gstreamer-app-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'app_dep'])
gstnet_dep = dependency('gstreamer-net-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_net_dep'])

gir = find_program('g-ir-scanner', required : false)
gnome = import('gnome')
build_gir = gir.found() and not meson.is_cross_build() and get_option('introspection')
gir_init_section = [ '--add-init-section=extern void gst_init(gint*,gchar**);' + \
    'g_setenv("GST_REGISTRY_1.0", "@0@", TRUE);'.format(meson.current_build_dir() + '/gir_empty_registry.reg') + \
    'g_setenv("GST_PLUGIN_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);' + \
    'gst_init(NULL,NULL);' ]

subdir('gst')
if get_option('tests')
  subdir('tests')
endif
if get_option('examples')
  subdir('examples')
endif
subdir('pkgconfig')
