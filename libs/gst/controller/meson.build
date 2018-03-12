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

mkenums = find_program('controller_mkenum.py')
gstcontroller_h = custom_target('gstcontrollerenum_h',
  output : 'controller-enumtypes.h',
  input : controller_mkenum_headers,
  install : true,
  install_dir : 'include/gstreamer-1.0/gst/controller/',
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gstcontroller_c = custom_target('gstcontrollerenum_c',
  output : 'controller-enumtypes.c',
  input : controller_mkenum_headers,
  depends : [gstcontroller_h],
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])
controller_gen_sources = [gstcontroller_h]

if libtype != 'shared'
  gst_controller_static = static_library('gstcontroller-@0@'.format(apiversion),
    gst_controller_sources, gstcontroller_h, gstcontroller_c,
    c_args : gst_c_args,
    install : true,
    include_directories : [configinc, libsinc],
    dependencies : [gobject_dep, glib_dep, mathlib, gst_dep],
  )
  gst_controller = gst_controller_static
endif


gst_controller_gen_sources = [gstcontroller_h]
if libtype != 'static'
  gst_controller_shared = shared_library('gstcontroller-@0@'.format(apiversion),
    gst_controller_sources, gstcontroller_h, gstcontroller_c,
    c_args : gst_c_args,
    install : true,
    version : libversion,
    soversion : soversion,
    include_directories : [configinc, libsinc],
    dependencies : [gobject_dep, glib_dep, mathlib, gst_dep],
  )
  gst_controller = gst_controller_shared
  if build_gir
    gst_gir_extra_args = gir_init_section + [ '--c-include=gst/controller/controller.h' ]
    gst_controller_gir = gnome.generate_gir(gst_controller_shared,
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
endif

gst_controller_dep = declare_dependency(link_with : gst_controller,
  include_directories : [libsinc],
  sources: gst_controller_gen_sources,
  dependencies : [gst_dep])
