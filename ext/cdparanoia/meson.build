cdparanoia_deps = []
cdparanoia_found = false

# cdparanoia upstream has a pkg-config file only in post-10.2 SVN so far, no release yet
cdparanoia_dep = dependency('cdparanoia-3', version : '>=10.2', required : false)
if cdparanoia_dep.found()
  cdparanoia_deps = [cdparanoia_dep]
  cdparanoia_found = true
else
  cdparanoia_dep = cc.find_library('cdda_paranoia', required : false)
  cdinterface_dep = cc.find_library('cdda_interface', required : false)
  if cdparanoia_dep.found() and cdinterface_dep.found()
    cdparanoia_deps = [cdparanoia_dep, cdinterface_dep]
    cdparanoia_found = true
    if cc.has_header_symbol('cdda/cdda_interface.h', 'cdda_open')
      core_conf.set('CDPARANOIA_HEADERS_IN_DIR', true)
    elif cc.has_header_symbol('cdda_interface.h', 'cdda_open')
      core_conf.set('CDPARANOIA_HEADERS_IN_DIR', false)
    endif
  endif
endif

if cdparanoia_found
  gstcdparanoia = library('gstcdparanoia',
    ['gstcdparanoiasrc.c'],
    include_directories: [configinc, libsinc],
    c_args : gst_plugins_base_args,
    dependencies : cdparanoia_deps + glib_deps + [audio_dep, gst_dep, gst_base_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
