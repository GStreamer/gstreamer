gst_elements_sources = [
  'gstcapsfilter.c',
  'gstclocksync.c',
  'gstconcat.c',
  'gstdataurisrc.c',
  'gstdownloadbuffer.c',
  'gstcoreelementsplugin.c',
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

gst_elements_headers = [
  'gstcapsfilter.h',
  'gstclocksync.h',
  'gstconcat.h',
  'gstcoreelementselements.h',
  'gstdataurisrc.h',
  'gstdownloadbuffer.h',
  'gstelements_private.h',
  'gstfakesink.h',
  'gstfakesrc.h',
  'gstfdsink.h',
  'gstfdsrc.h',
  'gstfilesink.h',
  'gstfilesrc.h',
  'gstfunnel.h',
  'gstidentity.h',
  'gstinputselector.h',
  'gstmultiqueue.h',
  'gstoutputselector.h',
  'gstqueue2.h',
  'gstqueue.h',
  'gstsparsefile.h',
  'gststreamiddemux.h',
  'gsttee.h',
  'gsttypefindelement.h',
  'gstvalve.h',
]

gst_elements = library('gstcoreelements',
  gst_elements_sources,
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gst_base_dep],
  install : true,
  install_dir : plugins_install_dir,
)

plugins += [gst_elements]

doc_sources = []
foreach s: gst_elements_sources + gst_elements_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'coreelements': pathsep.join(doc_sources)
}
