project('gst-devtools', 'c',
  version : '1.11.1.1',
  meson_version : '>= 0.36.0',
  default_options : [ 'warning_level=1',
                      'c_std=gnu99',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0]
gst_version_minor = version_arr[1]
gst_version_micro = version_arr[2]
if version_arr.length() == 4
  gst_version_nano = version_arr[3]
  TESTUITE_VERSION = 'master'
else
  gst_version_nano = 0
  TESTUITE_VERSION = '@0@.@1@'.format(gst_version_major, gst_version_minor)
endif

apiversion = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
libversion = '@0@.@1@.0'.format(soversion, gst_version_minor.to_int() * 100 + gst_version_micro.to_int())

prefix = get_option('prefix')

glib_req = '>= 2.40.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

cc = meson.get_compiler('c')

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
      language : 'c')
  # Disable SAFESEH with MSVC for plugins and libs that use external deps that
  # are built with MinGW
  noseh_link_args = ['/SAFESEH:NO']
else
  noseh_link_args = []
endif

gst_dep = dependency('gstreamer-' + apiversion, version : gst_req,
    fallback : ['gstreamer', 'gst_dep'])
gst_pbutils_dep = dependency('gstreamer-pbutils-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gst_video_dep = dependency('gstreamer-video-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
if host_machine.system() != 'windows'
  gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_check_dep'])
endif
glib_dep = dependency('glib-2.0', version: glib_req)
gio_dep = dependency('gio-2.0', version: glib_req)
gmodule_dep = dependency('gmodule-2.0', version: glib_req)
gtk_dep = dependency('gtk+-3.0', required: false)
mathlib = cc.find_library('m', required : false)
dl = cc.find_library('dl', required : false)
json_dep = dependency('json-glib-1.0',
    fallback : ['json-glib', 'json_glib_dep'])

gst_c_args = ['-DHAVE_CONFIG_H', '-DGST_USE_UNSTABLE_API']

gir_init_section = [ '--add-init-section=extern void gst_init(gint*,gchar**);' + \
    'g_setenv("GST_REGISTRY_1.0", "/no/way/this/exists.reg", TRUE);' + \
    'g_setenv("GST_PLUGIN_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);' + \
    'gst_init(NULL,NULL);' ]
gir = find_program('g-ir-scanner', required : false)
build_gir = gir.found() and not meson.is_cross_build() and not get_option('disable_introspection')
gnome = import('gnome')

gtkdoc = find_program('gtkdoc-scan', required : false)

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
        add_global_arguments('-Wno-unused', language: 'c')
    else
        message('GStreamer debug system is enabled')
    endif
endif

subdir('validate')

python3 = find_program('python3')
run_command(python3, '-c', 'import shutil; shutil.copy("hooks/multi-pre-commit.hook", ".git/hooks/pre-commit")')
