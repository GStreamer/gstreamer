gst_controller_sources = [
  'gstargbcontrolbinding.c',
  'gstdirectcontrolbinding.c',
  'gsttimedvaluecontrolsource.c',
  'gstinterpolationcontrolsource.c',
  'gstproxycontrolbinding.c',
  'gsttriggercontrolsource.c',
  'gstlfocontrolsource.c',
]

controller_mkenum_headers = [
  'gstinterpolationcontrolsource.h',
  'gstlfocontrolsource.h',
]

gst_controller_headers = controller_mkenum_headers + [
  'gstargbcontrolbinding.h',
  'gstdirectcontrolbinding.h',
  'gsttimedvaluecontrolsource.h',
  'gstinterpolationcontrolsource.h',
  'gstproxycontrolbinding.h',
  'gsttriggercontrolsource.h',
  'gstlfocontrolsource.h',
  'controller-prelude.h',
  'controller.h',
]
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
  c_args : gst_c_args + ['-DBUILDING_GST_CONTROLLER'],
  install : true,
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  include_directories : [configinc, libsinc],
  dependencies : [gobject_dep, glib_dep, mathlib, gst_dep],
)

if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/controller/controller.h' ]
  gst_controller_gir = gnome.generate_gir(gst_controller,
    sources : gst_controller_sources + gst_controller_headers + [gstcontroller_h] + [gstcontroller_c],
    namespace : 'GstController',
    nsversion : apiversion,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-controller-1.0',
    dependencies : [gst_dep],
    include_directories : [configinc, libsinc, privinc],
    includes : ['GLib-2.0', 'GObject-2.0', 'GModule-2.0', 'Gst-1.0'],
    install : true,
    extra_args : gst_gir_extra_args,
  )
  gst_controller_gen_sources += [gst_controller_gir]
endif

gst_controller_dep = declare_dependency(link_with : gst_controller,
  include_directories : [libsinc],
  sources: gst_controller_gen_sources,
  dependencies : [gst_dep])
