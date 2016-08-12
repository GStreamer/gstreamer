dvb_sources = [
  'camapplication.c',
  'camapplicationinfo.c',
  'cam.c',
  'camconditionalaccess.c',
  'camdevice.c',
  'camresourcemanager.c',
  'camsession.c',
  'camswclient.c',
  'camtransport.c',
  'camutils.c',
  'dvbbasebin.c',
  'gstdvb.c',
  'gstdvbsrc.c',
  'parsechannels.c',
]

dvb_check_code_lines = [
  '#include <linux/dvb/version.h>\n\n',
  '#if DVB_API_VERSION < 5 || (DVB_API_VERSION == 5 && DVB_API_VERSION_MINOR <= 4)\n\n',
  '#error "Incompatible dvb API"\n\n',
  '#endif',
]
dvb_check_code=''
foreach l : dvb_check_code_lines
  dvb_check_code = dvb_check_code + l
endforeach

if cc.compiles(dvb_check_code)
  gstdvb = library('gstdvb',
    dvb_sources,
    c_args : gst_plugins_bad_args + [ '-DGST_USE_UNSTABLE_API' ],
    include_directories : [configinc, libsinc],
    dependencies : [gstmpegts_dep, gstbase_dep, glib_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
