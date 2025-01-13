asf_sources = [
  'gstasfdemux.c',
  'gstasf.c',
  'gstasfelement.c',
  'asfheaders.c',
  'asfpacket.c',
  'gstrtpasfdepay.c',
  'gstrtspwms.c',
]

asf_headers = [
  'gstasfelements.h',
  'gstasfdemux.h',
  'asfheaders.h',
  'gstrtpasfdepay.h',
  'gstrtspwms.h',
  'asfpacket.h',
]

doc_sources = []
foreach s: asf_sources + asf_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'asf': pathsep.join(doc_sources)
}

if get_option('asfdemux').disabled()
  subdir_done()
endif

gstasf = library('gstasf',
  asf_sources,
  c_args : ugly_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstrtp_dep, gstvideo_dep,
                  gstaudio_dep, gsttag_dep, gstriff_dep,
                  gstrtsp_dep, gstsdp_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstasf]
