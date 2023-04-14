project('gstreamer-vaapi', 'c',
  version : '1.23.0.1',
  meson_version : '>= 0.62',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

if get_option('default_library') == 'static'
  error('GStreamer-VAAPI plugin not supported with `static` builds yet.')
endif

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

libva_req = ['>= 0.39.0', '!= 0.99.0']
libwayland_req = '>= 1.11.0'
libdrm_req = '>= 2.4.98'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

cc = meson.get_compiler('c')
static_build = get_option('default_library') == 'static'

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

# Mandatory GST deps
libm = cc.find_library('m', required : false)
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'allocators_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
gstcodecparsers_dep = dependency('gstreamer-codecparsers-1.0', version : gst_req,
    fallback : ['gst-plugins-bad', 'gstcodecparsers_dep'])
gstgl_dep = dependency('gstreamer-gl-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'gstgl_dep'], required: false)
gstglproto_dep = dependency('', required : false)
gstglx11_dep = dependency('', required : false)
gstglwayland_dep = dependency('', required : false)
gstglegl_dep = dependency('', required : false)

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
  add_project_arguments(cc.get_supported_arguments(['-Wno-unused']), language: 'c')
else
  message('GStreamer debug system is enabled')
endif

libva_dep = dependency('libva', version: libva_req,
  fallback : ['libva', 'libva_dep'])
libva_drm_dep = dependency('libva-drm', version: libva_req,
  required: get_option('drm'), fallback : ['libva', 'libva_drm_dep'])
libva_wayland_dep = dependency('libva-wayland', version: libva_req,
  required: get_option('wayland'), fallback : ['libva', 'libva_wayland_dep'])
libva_x11_dep = dependency('libva-x11', version: libva_req,
  required: get_option('x11'), fallback : ['libva', 'libva_x11_dep'])

libdrm_dep = dependency('libdrm', version: libdrm_req, required: get_option('drm'))
libudev_dep = dependency('libudev', required: get_option('drm'))

x11_dep = dependency('x11', required: get_option('x11'))
xrandr_dep = dependency('xrandr', required: get_option('x11'))

gmodule_dep = dependency('gmodule-no-export-2.0')
egl_dep = dependency('egl', required: get_option('egl'))
glesv2_dep = dependency('glesv2', required: false)

glx_option = get_option('glx').require(libva_x11_dep.found() and x11_dep.found(),
    error_message: 'glx requires libva-x11 and x11 dependency')
gl_dep = dependency('gl', required: glx_option)
libdl_dep = cc.find_library('dl', required: glx_option)

wayland_option = get_option('wayland').require(libdrm_dep.found(),
    error_message: 'wayland requires libdrm dependency')
wayland_client_dep = dependency('wayland-client', version: libwayland_req,
  required: wayland_option)
wayland_protocols_dep = dependency('wayland-protocols', version: '>= 1.15',
  required: wayland_option)
wayland_scanner_bin = find_program('wayland-scanner', required: wayland_option)

gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
  required : get_option('tests'),
  fallback : ['gstreamer', 'gst_check_dep'])

# some of the examples can use GTK+-3
gtk_dep = dependency('gtk+-3.0', version : '>= 3.10', required : get_option('examples'))

GLES_VERSION_MASK = gl_dep.found() ? 1 : 0
if glesv2_dep.found()
  if (cc.has_header('GLES2/gl2.h', dependencies: glesv2_dep) and
      cc.has_header('GLES2/gl2ext.h', dependencies: glesv2_dep))
    GLES_VERSION_MASK += 4
  endif
  if (cc.has_header('GLES3/gl3.h', dependencies: glesv2_dep) and
      cc.has_header('GLES3/gl3ext.h', dependencies: glesv2_dep) and
      cc.has_header('GLES2/gl2ext.h', dependencies: glesv2_dep))
    GLES_VERSION_MASK += 8
  endif
endif

USE_ENCODERS = get_option('encoders').allowed()
USE_VP9_ENCODER = USE_ENCODERS and libva_dep.version().version_compare('>= 0.40.0')
USE_AV1_DECODER = libva_dep.version().version_compare('>= 1.10')

USE_DRM = (libva_drm_dep.found()
    and libdrm_dep.found()
    and libudev_dep.found())
USE_EGL = (egl_dep.found()
    and GLES_VERSION_MASK != 0)
USE_WAYLAND = (libva_wayland_dep.found()
    and wayland_client_dep.found()
    and wayland_protocols_dep.found()
    and wayland_scanner_bin.found()
    and libdrm_dep.found())
USE_X11 = (libva_x11_dep.found() and x11_dep.found())
USE_GLX = (USE_X11 and gl_dep.found() and libdl_dep.found())

if not (USE_DRM or USE_X11 or USE_WAYLAND)
  error('No renderer API found (it is requried either DRM, X11 and/or WAYLAND)')
endif

if gstgl_dep.found()
  gstglproto_dep = dependency('gstreamer-gl-prototypes-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'gstglproto_dep'], required: true)
  # Behind specific checks because meson fails at optional dependencies with a
  # fallback to the same subproject.  On the first failure, meson will never
  # check the system again even if the fallback never existed.
  # Last checked with meson 0.54.3
  if USE_X11
    gstglx11_dep = dependency('gstreamer-gl-x11-1.0', version : gst_req,
       fallback : ['gst-plugins-base', 'gstglx11_dep'], required: true)
  endif
  if USE_WAYLAND
    gstglwayland_dep = dependency('gstreamer-gl-wayland-1.0', version : gst_req,
        fallback : ['gst-plugins-base', 'gstglwayland_dep'], required: true)
  endif
  if USE_EGL
    gstglegl_dep = dependency('gstreamer-gl-egl-1.0', version : gst_req,
        fallback : ['gst-plugins-base', 'gstglegl_dep'], required: true)
  endif
endif

driverdir = libva_dep.get_variable('driverdir', default_value: '')
if driverdir == ''
  driverdir = join_paths(get_option('prefix'), get_option('libdir'), 'dri')
endif

cdata = configuration_data()
cdata.set_quoted('GST_API_VERSION_S', '@0@.@1@'.format(gst_version_major, gst_version_minor))
cdata.set_quoted('PACKAGE', 'gstreamer-vaapi')
cdata.set_quoted('VERSION', '@0@'.format(gst_version))
cdata.set_quoted('PACKAGE_VERSION', '@0@'.format(gst_version))
cdata.set_quoted('PACKAGE_NAME', 'GStreamer VA-API Plug-ins')
cdata.set_quoted('PACKAGE_STRING', 'GStreamer VA-API Plug-ins @0@'.format(gst_version))
cdata.set_quoted('PACKAGE_BUGREPORT', get_option('package-origin'))
cdata.set_quoted('VA_DRIVERS_PATH', '@0@'.format(driverdir))
cdata.set10('GST_VAAPI_USE_DRM', USE_DRM)
cdata.set10('GST_VAAPI_USE_EGL', USE_EGL)
cdata.set10('GST_VAAPI_USE_ENCODERS', USE_ENCODERS)
cdata.set10('GST_VAAPI_USE_GLX', USE_GLX)
cdata.set10('GST_VAAPI_USE_VP9_ENCODER', USE_VP9_ENCODER)
cdata.set10('GST_VAAPI_USE_AV1_DECODER', USE_AV1_DECODER)
cdata.set10('GST_VAAPI_USE_WAYLAND', USE_WAYLAND)
cdata.set10('GST_VAAPI_USE_X11', USE_X11)
cdata.set10('HAVE_XKBLIB', cc.has_header('X11/XKBlib.h', dependencies: x11_dep))
cdata.set10('HAVE_XRANDR', xrandr_dep.found())
cdata.set10('USE_GST_GL_HELPERS', gstgl_dep.found())
cdata.set('USE_GLES_VERSION_MASK', GLES_VERSION_MASK)

api_version = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
curversion = gst_version_minor * 100 + gst_version_micro
libversion = '@0@.@1@.0'.format(soversion, curversion)
osxversion = curversion + 1

plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

gstreamer_vaapi_args = ['-DHAVE_CONFIG_H']
configinc = include_directories('.')
libsinc = include_directories('gst-libs')

plugins = []

subdir('gst-libs')
subdir('gst')
subdir('tests')
subdir('docs')

# Set release date
if gst_version_nano == 0
  extract_release_date = find_program('scripts/extract-release-date-from-doap-file.py')
  run_result = run_command(extract_release_date, gst_version, files('gstreamer-vaapi.doap'), check: true)
  release_date = run_result.stdout().strip()
  cdata.set_quoted('GST_PACKAGE_RELEASE_DATETIME', release_date)
  message('Package release date: ' + release_date)
endif

if gmodule_dep.version().version_compare('< 2.67.4')
  cdata.set('g_memdup2(ptr,sz)', '(G_LIKELY(((guint64)(sz)) < G_MAXUINT)) ? g_memdup(ptr,sz) : (g_abort(),NULL)')
endif

configure_file(output: 'config.h', configuration: cdata)

meson.add_dist_script('scripts/gen-changelog.py', meson.project_name(), '1.20.0', meson.project_version())

pkgconfig = import('pkgconfig')
plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

plugin_names = []
gst_plugins = []
foreach plugin: plugins
  pkgconfig.generate(plugin, install_dir: plugins_pkgconfig_install_dir)
  dep = declare_dependency(link_with: plugin, variables: {'full_path': plugin.full_path()})
  meson.override_dependency(plugin.name(), dep)
  gst_plugins += [dep]
  if plugin.name().startswith('gst')
    plugin_names += [plugin.name().substring(3)]
  else
    plugin_names += [plugin.name()]
  endif
endforeach

summary({
    'Plugins': plugin_names,
}, list_sep: ', ')
