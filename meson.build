project('gst-devtools', 'c',
  version : '1.9.90',
  meson_version : '>= 0.33.0',
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
  TESTUITE_VERSION = '@0@.@1@'.format(gst_version_major, gst_version_minor)
else
  gst_version_nano = 0
  TESTUITE_VERSION = 'master'
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
gst_dep = dependency('gstreamer-' + apiversion, version : gst_req,
    fallback : ['gstreamer', 'gst_dep'])
gst_pbutils_dep = dependency('gstreamer-pbutils-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gst_video_dep = dependency('gstreamer-video-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
glib_dep = dependency('glib-2.0', version: glib_req)
gio_dep = dependency('gio-2.0', version: glib_req)
gmodule_dep = dependency('gmodule-2.0', version: glib_req)
gtk_dep = dependency('gtk+-3.0', required: false)
mathlib = cc.find_library('m', required : false)

gst_c_args = ['-DHAVE_CONFIG_H', '-DGST_USE_UNSTABLE_API']

gir_init_section = [ '--add-init-section=extern void gst_init(gint*,gchar**); gst_init(NULL,NULL);' ]
gir = find_program('g-ir-scanner', required : false)
build_gir = gir.found() and not meson.is_cross_build() and not get_option('disable_introspection')
gnome = import('gnome')

gtkdoc = find_program('gtkdoc-scan', required : false)

subdir('validate')

python3 = find_program('python3')
run_command(python3, '-c', 'import shutil; shutil.copy("hooks/multi-pre-commit.hook", ".git/hooks/pre-commit")')
