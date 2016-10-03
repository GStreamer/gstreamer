project('gst-editing-services', 'c',
  version : '1.9.90',
  meson_version : '>= 0.35.0',
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
else
  gst_version_nano = 0
endif

apiversion = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
libversion = '@0@.@1@.0'.format(soversion, gst_version_minor.to_int() * 100 + gst_version_micro.to_int())

glib_req = '>= 2.40.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

cc = meson.get_compiler('c')
# Ignore several spurious warnings for things gstreamer does very commonly
# If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
# If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
# NOTE: Only add warnings here if you are sure they're spurious
if cc.get_id() == 'msvc'
  add_global_arguments('/wd4018', '/wd4244', '/wd4996', language : 'c')
endif

cdata = configuration_data()
cdata.set('VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE', '"gst-editing-services"')
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE_BUGREPORT', '"http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer"')
cdata.set('PACKAGE_NAME', '"GStreamer Editing Services"')
cdata.set('GST_PACKAGE_NAME', '"GStreamer Editing Services"')
cdata.set('GST_PACKAGE_ORIGIN', '"Unknown package origin"')
cdata.set('GST_LICENSE', '"LGPL"')
cdata.set('LIBDIR', '"@0@"'.format(get_option('libdir')))

# Mandatory GST deps
gst_dep = dependency('gstreamer-' + apiversion, version : gst_req,
    fallback : ['gstreamer', 'gst_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gstvideo_dep = dependency('gstreamer-video-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_base_dep'])
gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_check_dep'])
gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])
gstvalidate_dep = dependency('gst-validate-1.0', version : gst_req, required : false,
  fallback : ['gst-devtools', 'validate_dep'])

gio_dep = dependency('gio-2.0', version : glib_req)
libxml_dep = dependency('libxml-2.0')

# TODO Properly port to Gtk 3
# gtk_dep = dependency('gtk+-3.0', required : false)

libges_deps = [gst_dep, gstbase_dep, gstvideo_dep, gstpbutils_dep,
               gstcontroller_dep, gio_dep, libxml_dep]

if gstvalidate_dep.found()
    libges_deps = libges_deps + [gstvalidate_dep]
    cdata.set('HAVE_GST_VALIDATE', 1)
endif

configure_file(input : 'config.h.meson',
  output : 'config.h',
  configuration : cdata)


gir = find_program('g-ir-scanner', required : false)
gnome = import('gnome')
gtkdoc = find_program('gtkdoc-scan', required : false)

# Fixme, not very elegant.
build_gir = gir.found() and not meson.is_cross_build() and not get_option('disable_introspection')
gir_init_section = [ '--add-init-section=extern void gst_init(gint*,gchar**); gst_init(NULL,NULL);' ]

ges_c_args = ['-DHAVE_CONFIG_H']
plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

configinc = include_directories('.')
subdir('ges')
subdir('plugins')
subdir('tools')
subdir('pkgconfig')
subdir('tests')
subdir('examples')

if build_machine.system() != 'windows'
  if gtkdoc.found()
    subdir('docs')
  else
    message('Not building documentation as gtk-doc was not found')
  endif
else
  message('Disabling gtk-doc while building on Windows')
endif

python3 = find_program('python3')
run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
