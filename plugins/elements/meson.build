gst_elements_sources = [
  'gstcapsfilter.c',
  'gstconcat.c',
  'gstdataurisrc.c',
  'gstdownloadbuffer.c',
  'gstelements.c',
  'gstelements_private.c',
  'gstfakesink.c',
  'gstfakesrc.c',
  'gstfdsink.c',
  'gstfdsrc.c',
  'gstfilesrc.c',
  'gstfilesink.c',
  'gstfunnel.c',
  'gstidentity.c',
  'gstinputselector.c',
  'gstmultiqueue.c',
  'gstoutputselector.c',
  'gstqueue2.c',
  'gstqueue.c',
  'gstsparsefile.c',
  'gststreamiddemux.c',
  'gsttee.c',
  'gsttypefindelement.c',
  'gstvalve.c',
]

if libtype != 'shared'
  gst_elements_static = static_library('gstcoreelements',
    gst_elements_sources,
    c_args : gst_c_args + ['-DGST_PLUGIN_BUILD_STATIC'],
    include_directories : [configinc],
    dependencies : [gobject_dep, glib_dep, gst_dep, gst_base_dep],
    install : true,
    install_dir : '@0@/gstreamer-1.0'.format(get_option('libdir')),
  )
  gst_elements = gst_elements_static
endif

if libtype != 'static'
  gst_elements_shared = shared_library('gstcoreelements',
    gst_elements_sources,
    c_args : gst_c_args,
    include_directories : [configinc],
    dependencies : [gobject_dep, glib_dep, gst_dep, gst_base_dep],
    install : true,
    install_dir : '@0@/gstreamer-1.0'.format(get_option('libdir')),
  )
  gst_elements = gst_elements_shared
endif
