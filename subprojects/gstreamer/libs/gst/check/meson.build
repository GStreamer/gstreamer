gst_check_sources = files(
  'gstbufferstraw.c',
  'gstcheck.c',
  'gstconsistencychecker.c',
  'gstharness.c',
  'gstharnesslink.c',
  'gsttestclock.c',
)
gst_check_headers = files(
  'check.h',
  'check-prelude.h',
  'gstbufferstraw.h',
  'gstcheck.h',
  'gstconsistencychecker.h',
  'gstharness.h',
  'gsttestclock.h',
)
install_headers(gst_check_headers, subdir : 'gstreamer-1.0/gst/check/')


check_cdata = configuration_data()

check_cdata.set('ENABLE_SUBUNIT', 0)
check_cdata.set('CHECK_MAJOR_VERSION', 0)
check_cdata.set('CHECK_MINOR_VERSION', 9)
check_cdata.set('CHECK_MICRO_VERSION', 14)
if host_system != 'windows'
  check_cdata.set('HAVE_FORK', 1)
else
  check_cdata.set('HAVE_FORK', 0)
endif

subdir('libcheck')

configure_file(input : 'libcheck/check.h.in',
  output : 'internal-check.h',
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst/check'),
  configuration : check_cdata)

gst_check = library('gstcheck-@0@'.format(apiversion),
  gst_check_sources,
  c_args : gst_c_args + ['-UG_DISABLE_ASSERT', '-DBUILDING_GST_CHECK', '-DG_LOG_DOMAIN="GStreamer-Check"'],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  include_directories : [configinc, libsinc],
  link_with : [libcheck],
  dependencies : [gst_dep],
)

library_def = {'lib': gst_check}
pkg_name = 'gstreamer-check-1.0'
pkgconfig.generate(gst_check,
  libraries : [libgst],
  # FIXME: Add manually libcheck's dependencies because it's an uninstalled static
  # library and Meson <0.56.0 does not handle them correctly.
  # See https://github.com/mesonbuild/meson/pull/7488.
  libraries_private: [rt_lib, mathlib],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'Unit testing helper library for GStreamer modules',
)

gst_check_gen_sources = []
if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/check/check.h' ]
  gir = {
    'sources' : gst_check_sources + gst_check_headers,
    'namespace' : 'GstCheck',
    'nsversion' : apiversion,
    'identifier_prefix' : 'Gst',
    'symbol_prefix' : 'gst',
    'export_packages' : pkg_name,
    'dependencies' : [gst_dep],
    'include_directories' : [configinc, libsinc, privinc],
    'includes' : ['GLib-2.0', 'GObject-2.0', 'GModule-2.0', 'Gst-1.0'],
    'install' : true,
    'extra_args' : gst_gir_extra_args,
  }
  library_def += {'gir': [gir]}
  if not static_build
    gst_check_gir = gnome.generate_gir(gst_check, kwargs: gir)
    library_def += {'gir_targets':  library_def.get('gir_targets', []) + [gst_check_gir]}
    gst_check_gen_sources += gst_check_gir
  endif
endif
gst_libraries += [[pkg_name, library_def]]

gst_check_dep = declare_dependency(link_with : gst_check,
  include_directories : [libsinc],
  dependencies : [gst_dep],
  sources : gst_check_gen_sources)

meson.override_dependency(pkg_name, gst_check_dep)
