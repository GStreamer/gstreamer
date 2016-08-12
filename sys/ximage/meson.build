x11_dep = dependency('x11', required : false)

if x11_dep.found()
  x_args = []
  # FIXME: Need to check for XShmAttach inside libXext
  xshm_dep = dependency('xext', required : false)
  if xshm_dep.found()
    x_args += ['-DHAVE_XSHM']
  endif

  xfixes_dep = dependency('xfixes', required : false)
  if xfixes_dep.found()
    x_args += ['-DHAVE_XFIXES']
  endif

  xdamage_dep = dependency('xdamage', required : false)
  if xdamage_dep.found()
    x_args += ['-DHAVE_XDAMAGE']
  endif

  gstximagesrc = library('gstximagesrc',
    'gstximagesrc.c', 'ximageutil.c',
    c_args : gst_plugins_good_args + x_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstbase_dep, gstvideo_dep, x11_dep,
                    xshm_dep, xfixes_dep, xdamage_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
