gst_base_sources = [
  'gstadapter.c',
  'gstbaseparse.c',
  'gstbasesink.c',
  'gstbasesrc.c',
  'gstbasetransform.c',
  'gstbitreader.c',
  'gstbytereader.c',
  'gstbytewriter.c',
  'gstcollectpads.c',
  'gstdataqueue.c',
  'gstflowcombiner.c',
  'gstpushsrc.c',
  'gstqueuearray.c',
  'gsttypefindhelper.c',
]

gst_base_headers = [
  'base.h',
  'gstadapter.h',
  'gstbaseparse.h',
  'gstbasesink.h',
  'gstbasesrc.h',
  'gstbasetransform.h',
  'gstbitreader.h',
  'gstbytereader.h',
  'gstbytewriter.h',
  'gstcollectpads.h',
  'gstdataqueue.h',
  'gstflowcombiner.h',
  'gstpushsrc.h',
  'gstqueuearray.h',
  'gsttypefindhelper.h',
]

if libtype != 'shared'
  gst_base_static = static_library('gstbase-@0@'.format(apiversion),
    gst_base_sources,
    c_args : gst_c_args,
    install : true,
    include_directories : [configinc, libsinc],
    dependencies : [gobject_dep, glib_dep, gst_dep],
  )
  gst_base = gst_base_static
endif

gst_base_gen_sources = []

if libtype != 'static'
  gst_base_shared = shared_library('gstbase-@0@'.format(apiversion),
    gst_base_sources,
    c_args : gst_c_args,
    version : libversion,
    soversion : soversion,
    install : true,
    include_directories : [configinc, libsinc],
    dependencies : [gobject_dep, glib_dep, gst_dep],
    vs_module_defs: vs_module_defs_dir + 'libgstbase.def',
  )
  gst_base = gst_base_shared
  if build_gir
    gst_gir_extra_args = gir_init_section + [ '--c-include=gst/base/base.h' ]
    gst_base_gen_sources += [gnome.generate_gir(gst_base_shared,
      sources : gst_base_sources + gst_base_headers,
      namespace : 'GstBase',
      nsversion : apiversion,
      identifier_prefix : 'Gst',
      symbol_prefix : 'gst',
      export_packages : 'gstreamer-base-1.0',
      dependencies : [gst_dep],
      include_directories : [configinc, libsinc, privinc],
      includes : ['GLib-2.0', 'GObject-2.0', 'GModule-2.0', 'Gst-1.0'],
      install : true,
      extra_args : gst_gir_extra_args,
    )]
  endif
endif

gst_base_dep = declare_dependency(link_with : gst_base,
  include_directories : [libsinc],
  dependencies : [gst_dep],
  sources : gst_base_gen_sources)

install_headers('base.h',
  'gstadapter.h',
  'gstbaseparse.h',
  'gstbasesink.h',
  'gstbasesrc.h',
  'gstbasetransform.h',
  'gstbitreader.h',
  'gstbytereader.h',
  'gstbytewriter.h',
  'gstcollectpads.h',
  'gstdataqueue.h',
  'gstflowcombiner.h',
  'gstpushsrc.h',
  'gstqueuearray.h',
  'gsttypefindhelper.h',
  subdir : 'gstreamer-1.0/gst/base',
)
