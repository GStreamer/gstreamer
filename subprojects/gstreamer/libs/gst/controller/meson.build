gst_controller_sources = files(
  'gstargbcontrolbinding.c',
  'gstdirectcontrolbinding.c',
  'gsttimedvaluecontrolsource.c',
  'gstinterpolationcontrolsource.c',
  'gstproxycontrolbinding.c',
  'gsttriggercontrolsource.c',
  'gstlfocontrolsource.c',
)

controller_mkenum_headers = files(
  'gstinterpolationcontrolsource.h',
  'gstlfocontrolsource.h',
)

gst_controller_headers = controller_mkenum_headers + files(
  'gstargbcontrolbinding.h',
  'gstdirectcontrolbinding.h',
  'gsttimedvaluecontrolsource.h',
  'gstinterpolationcontrolsource.h',
  'gstproxycontrolbinding.h',
  'gsttriggercontrolsource.h',
  'gstlfocontrolsource.h',
  'controller-prelude.h',
  'controller.h',
)
install_headers(gst_controller_headers, subdir : 'gstreamer-1.0/gst/controller/')

controller_enums = gnome.mkenums_simple('controller-enumtypes',
  sources : controller_mkenum_headers,
  header_prefix : '#include <gst/controller/controller-prelude.h>',
  body_prefix : '#include "config.h"',
  decorator : 'GST_CONTROLLER_API',
  install_header : true,
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst/controller'))
gstcontroller_c = controller_enums[0]
gstcontroller_h = controller_enums[1]

gst_controller_gen_sources = [gstcontroller_h]
gst_controller = library('gstcontroller-@0@'.format(apiversion),
  gst_controller_sources, gstcontroller_h, gstcontroller_c,
  c_args : gst_c_args + ['-DBUILDING_GST_CONTROLLER', '-DG_LOG_DOMAIN="GStreamer-Controller"'],
  install : true,
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  include_directories : [configinc, libsinc],
  dependencies : [mathlib, gst_dep],
)

library_def = {'lib': gst_controller}
pkg_name = 'gstreamer-controller-1.0'
pkgconfig.generate(gst_controller,
  libraries : [libgst],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'Dynamic parameter control for GStreamer elements',
)

if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/controller/controller.h' ]
  gir = {
    'sources' : gst_controller_sources + gst_controller_headers + [gstcontroller_h] + [gstcontroller_c],
    'namespace': 'GstController',
    'nsversion': apiversion,
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
    gst_controller_gir = gnome.generate_gir(gst_controller, kwargs: gir)
    library_def += {'gir_targets':  library_def.get('gir_targets', []) + [gst_controller_gir]}
    gst_controller_gen_sources += [gst_controller_gir]
  endif
endif
gst_libraries += [[pkg_name, library_def]]

gst_controller_dep = declare_dependency(link_with : gst_controller,
  include_directories : [libsinc],
  sources: gst_controller_gen_sources,
  dependencies : [gst_dep])

meson.override_dependency(pkg_name, gst_controller_dep)
