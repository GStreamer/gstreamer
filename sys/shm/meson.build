shm_sources = [
  'shmpipe.c',
  'shmalloc.c',
  'gstshm.c',
  'gstshmsrc.c',
  'gstshmsink.c',
]

shm_enabled = false
shm_deps = []
rt_dep = cc.find_library ('rt', required: false)

if cc.has_header ('sys/socket.h') and (host_system == 'osx' or
    host_system == 'bsd' or rt_dep.found())

  shm_enabled = true
  shm_deps = [gstbase_dep]

  if rt_dep.found()
    shm_deps += [rt_dep]
  endif
  
  gstshm = library('gstshm',
    shm_sources,
    c_args : gst_plugins_bad_args + ['-DSHM_PIPE_USE_GLIB'],
    include_directories : [configinc],
    dependencies : shm_deps,
    install : true,
    install_dir : plugins_install_dir,
  )
endif
