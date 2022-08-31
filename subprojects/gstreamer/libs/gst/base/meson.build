gst_base_sources = files(
  'gstadapter.c',
  'gstaggregator.c',
  'gstbaseparse.c',
  'gstbasesink.c',
  'gstbasesrc.c',
  'gstbasetransform.c',
  'gstbitreader.c',
  'gstbitwriter.c',
  'gstbytereader.c',
  'gstbytewriter.c',
  'gstcollectpads.c',
  'gstdataqueue.c',
  'gstflowcombiner.c',
  'gstpushsrc.c',
  'gstqueuearray.c',
  'gsttypefindhelper.c',
)

gst_base_headers = files(
  'base.h',
  'base-prelude.h',
  'gstadapter.h',
  'gstaggregator.h',
  'gstbaseparse.h',
  'gstbasesink.h',
  'gstbasesrc.h',
  'gstbasetransform.h',
  'gstbitreader.h',
  'gstbitwriter.h',
  'gstbytereader.h',
  'gstbytewriter.h',
  'gstcollectpads.h',
  'gstdataqueue.h',
  'gstflowcombiner.h',
  'gstpushsrc.h',
  'gstqueuearray.h',
  'gsttypefindhelper.h',
)

gst_base = library('gstbase-@0@'.format(apiversion),
  gst_base_sources,
  c_args : gst_c_args + ['-DBUILDING_GST_BASE', '-DG_LOG_DOMAIN="GStreamer-Base"'],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep],
)

library_def = {'lib': gst_base}
pkg_name = 'gstreamer-base-1.0'
gst_base_gen_sources = []
pkgconfig.generate(gst_base,
  libraries : [libgst],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'Base classes for GStreamer elements',
)

if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/base/base.h' ]
  gir = {
    'sources' : gst_base_sources + gst_base_headers,
    'namespace' : 'GstBase',
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
    gst_base_gir = gnome.generate_gir(gst_base, kwargs: gir)
    library_def += {'gir_targets':  library_def.get('gir_targets', []) + [gst_base_gir]}
    gst_base_gen_sources += [gst_base_gir]
  endif
endif
gst_libraries += [[pkg_name, library_def]]

gst_base_dep = declare_dependency(link_with : gst_base,
  include_directories : [libsinc],
  dependencies : [gst_dep],
  sources : gst_base_gen_sources)

meson.override_dependency(pkg_name, gst_base_dep)

install_headers('base.h',
  'base-prelude.h',
  'gstadapter.h',
  'gstaggregator.h',
  'gstbaseparse.h',
  'gstbasesink.h',
  'gstbasesrc.h',
  'gstbasetransform.h',
  'gstbitreader.h',
  'gstbitwriter.h',
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
