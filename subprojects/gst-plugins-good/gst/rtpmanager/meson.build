rtpmanager_sources = [
  'gstrtpmanager.c',
  'gstrtpbin.c',
  'gstrtpdtmfmux.c',
  'gstrtpjitterbuffer.c',
  'gstrtphdrext-twcc.c',
  'gstrtphdrext-clientaudiolevel.c',
  'gstrtphdrext-mid.c',
  'gstrtphdrext-ntp.c',
  'gstrtphdrext-repairedstreamid.c',
  'gstrtphdrext-streamid.c',
  'gstrtpmux.c',
  'gstrtpptdemux.c',
  'gstrtprtxqueue.c',
  'gstrtprtxreceive.c',
  'gstrtprtxsend.c',
  'gstrtpssrcdemux.c',
  'rtpjitterbuffer.c',
  'rtpsession.c',
  'rtpsource.c',
  'rtpstats.c',
  'rtptimerqueue.c',
  'rtptwcc.c',
  'gstrtpsession.c',
  'gstrtpfunnel.c',
  'gstrtpst2022-1-fecdec.c',
  'gstrtpst2022-1-fecenc.c',
  'gstrtputils.c'
]

rtpmanager_headers = [
  'gstrtprtxreceive.h',
  'rtpsession.h',
  'gstrtphdrext-repairedstreamid.h',
  'gstrtprtxqueue.h',
  'rtpsource.h',
  'gstrtpptdemux.h',
  'gstrtpmux.h',
  'gstrtphdrext-mid.h',
  'rtptwcc.h',
  'gstrtprtxsend.h',
  'gstrtphdrext-streamid.h',
  'gstrtpst2022-1-fecenc.h',
  'rtpjitterbuffer.h',
  'rtpstats.h',
  'gstrtpssrcdemux.h',
  'gstrtpbin.h',
  'gstrtphdrext-twcc.h',
  'gstrtphdrext-clientaudiolevel.h',
  'gstrtpfunnel.h',
  'gstrtpjitterbuffer.h',
  'rtptimerqueue.h',
  'gstrtputils.h',
  'gstrtpsession.h',
  'gstrtphdrext-ntp.h',
  'gstrtpst2022-1-fecdec.h',
  'gstrtpdtmfmux.h',
]

doc_sources = []
foreach s: rtpmanager_sources + rtpmanager_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'rtpmanager': pathsep.join(doc_sources)
}

if get_option('rtpmanager').disabled()
  subdir_done()
endif

gstrtpmanager = library('gstrtpmanager',
  rtpmanager_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstnet_dep, gstrtp_dep, gstaudio_dep, gio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstrtpmanager]
