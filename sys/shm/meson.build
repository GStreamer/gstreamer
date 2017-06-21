shm_sources = [
  'shmpipe.c',
  'shmalloc.c',
  'gstshm.c',
  'gstshmsrc.c',
  'gstshmsink.c',
]

rt_dep = cc.find_library ('rt', required: false)

if cc.has_header ('sys/socket.h') and (host_system == 'osx' or
    host_system == 'bsd' or rt_dep.found())

  deps = [gstbase_dep]

  if rt_dep.found()
    deps += [rt_dep]
  endif
  
  gstshm = library('gstshm',
    shm_sources,
    c_args : gst_plugins_bad_args + ['-DSHM_PIPE_USE_GLIB'],
    include_directories : [configinc],
    dependencies : deps,
    install : true,
    install_dir : plugins_install_dir,
  )
endif
