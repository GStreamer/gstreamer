if get_option('vcd').disabled()
  subdir_done()
endif

if cc.has_header ('linux/cdrom.h')
  gstvcd = library('gstvcdsrc', 'vcdsrc.c',
    c_args: gst_plugins_bad_args,
    include_directories: [configinc],
    dependencies : [gstbase_dep],
    install: true,
    install_dir: plugins_install_dir
  )
  pkgconfig.generate(gstvcd, install_dir: plugins_pkgconfig_install_dir)
elif get_option('vcd').enabled()
  error('vcd plugin enabled but linux/cdrom.h not found')
endif
