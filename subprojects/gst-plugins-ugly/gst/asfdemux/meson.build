asf_sources = [
  'gstasfdemux.c',
  'gstasf.c',
  'gstasfelement.c',
  'asfheaders.c',
  'asfpacket.c',
  'gstrtpasfdepay.c',
  'gstrtspwms.c',
]

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
