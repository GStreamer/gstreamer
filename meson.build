project('gst-editing-services', 'c',
  version : '1.14.1',
  meson_version : '>= 0.36.0',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
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

apiversion = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
libversion = '@0@.@1@.0'.format(soversion, gst_version_minor * 100 + gst_version_micro)

glib_req = '>= 2.40.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

cc = meson.get_compiler('c')
# Ignore several spurious warnings for things gstreamer does very commonly
# If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
# If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
# NOTE: Only add warnings here if you are sure they're spurious
if cc.get_id() == 'msvc'
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

cdata = configuration_data()
cdata.set('VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE', '"gst-editing-services"')
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE_BUGREPORT', '"http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer"')
cdata.set('PACKAGE_NAME', '"GStreamer Editing Services"')
cdata.set('GST_PACKAGE_NAME', '"GStreamer Editing Services"')
cdata.set('GST_PACKAGE_ORIGIN', '"Unknown package origin"')
cdata.set('GST_LICENSE', '"LGPL"')

# Mandatory GST deps
gst_dep = dependency('gstreamer-' + apiversion, version : gst_req,
    fallback : ['gstreamer', 'gst_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gstvideo_dep = dependency('gstreamer-video-' + apiversion, version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_base_dep'])
if host_machine.system() != 'windows'
  gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_check_dep'], required: false)
endif
gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])
gstvalidate_dep = dependency('gst-validate-1.0', version : gst_req, required : false,
  fallback : ['gst-devtools', 'validate_dep'])

gio_dep = dependency('gio-2.0', fallback: ['glib', 'libgio_dep'])
libxml_dep = dependency('libxml-2.0')

# TODO Properly port to Gtk 3
# gtk_dep = dependency('gtk+-3.0', required : false)

libges_deps = [gst_dep, gstbase_dep, gstvideo_dep, gstpbutils_dep,
               gstcontroller_dep, gio_dep, libxml_dep]

if gstvalidate_dep.found()
    libges_deps = libges_deps + [gstvalidate_dep]
    cdata.set('HAVE_GST_VALIDATE', 1)
endif

configure_file(output : 'config.h', configuration : cdata)


gir = find_program('g-ir-scanner', required : false)
gnome = import('gnome')

# Fixme, not very elegant.
build_gir = gir.found() and not meson.is_cross_build() and not get_option('disable_introspection')
gir_init_section = [ '--add-init-section=' + \
    'extern void gst_init(gint*,gchar**);' + \
    'extern void ges_init(void);' + \
    'g_setenv("GST_REGISTRY_1.0", "/no/way/this/exists.reg", TRUE);' + \
    'g_setenv("GST_PLUGIN_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_DEBUG", "0", TRUE);' + \
    'gst_init(NULL,NULL);' + \
    'ges_init();' ]

ges_c_args = ['-DHAVE_CONFIG_H', '-DG_LOG_DOMAIN="GES"']
plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

if gst_dep.type_name() == 'internal'
  gst_debug_disabled = subproject('gstreamer').get_variable('disable_gst_debug')
else
  # We can't check that in the case of subprojects as we won't
  # be able to build against an internal dependency (which is not built yet)
  gst_debug_disabled = cc.has_header_symbol('gst/gstconfig.h', 'GST_DISABLE_GST_DEBUG', dependencies: gst_dep)
endif

if gst_debug_disabled and cc.has_argument('-Wno-unused')
  add_project_arguments('-Wno-unused', language: 'c')
endif

warning_flags = [
  '-Wmissing-declarations',
  '-Wmissing-prototypes',
  '-Wredundant-decls',
  '-Wundef',
  '-Wwrite-strings',
  '-Wformat',
  '-Wformat-security',
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

configinc = include_directories('.')
subdir('ges')
subdir('plugins')
subdir('tools')
subdir('pkgconfig')
subdir('tests')
subdir('examples')

if build_machine.system() == 'windows'
  message('Disabling gtk-doc while building on Windows')
elif get_option('disable_gtkdoc')
  message('gtk-doc is disabled via options')
else
  if find_program('gtkdoc-scan', required : false).found()
    subdir('docs')
  else
    message('Not building documentation as gtk-doc was not found')
  endif
endif

python3 = import('python3').find_python()
run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
