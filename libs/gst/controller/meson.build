gst_controller_sources = [
  'gstargbcontrolbinding.c',
  'gstdirectcontrolbinding.c',
  'gsttimedvaluecontrolsource.c',
  'gstinterpolationcontrolsource.c',
  'gsttriggercontrolsource.c',
  'gstlfocontrolsource.c',
]

gst_controller_headers = [
  'gstargbcontrolbinding.h',
  'gstdirectcontrolbinding.h',
  'gsttimedvaluecontrolsource.h',
  'gstinterpolationcontrolsource.h',
  'gsttriggercontrolsource.h',
  'gstlfocontrolsource.h',
  'controller.h',
]
install_headers(gst_controller_headers, subdir : 'gstreamer-1.0/gst/controller/')

if libtype != 'shared'
  gst_controller_static = static_library('gstcontroller-@0@'.format(apiversion),
    gst_controller_sources,
    c_args : gst_c_args,
    install : true,
    include_directories : [configinc, libsinc],
    dependencies : [gobject_dep, glib_dep, mathlib, gst_dep],
  )
  gst_controller = gst_controller_static
endif

if libtype != 'static'
  gst_controller_shared = shared_library('gstcontroller-@0@'.format(apiversion),
    gst_controller_sources,
    c_args : gst_c_args,
    install : true,
    version : libversion,
    soversion : soversion,
    include_directories : [configinc, libsinc],
    dependencies : [gobject_dep, glib_dep, mathlib, gst_dep],
    vs_module_defs: vs_module_defs_dir + 'libgstcontroller.def',
  )
  gst_controller = gst_controller_shared
  if build_gir
    gnome.generate_gir(gst_controller_shared,
      sources : gst_controller_sources + gst_controller_headers,
      namespace : 'GstController',
      nsversion : apiversion,
      identifier_prefix : 'Gst',
      symbol_prefix : 'gst',
      export_packages : 'gstreamer-controller-1.0',
      dependencies : [gst_dep],
      include_directories : [configinc, libsinc, privinc],
      includes : ['GLib-2.0', 'GObject-2.0', 'GModule-2.0', 'Gst-1.0'],
      install : true,
      extra_args : gir_init_section,
    )
  endif
endif

gst_controller_dep = declare_dependency(link_with : gst_controller,
  include_directories : [libsinc],
  dependencies : [gst_dep])
