ipcpipeline_sources = [
  'gstipcpipeline.c',
  'gstipcpipelinecomm.c',
  'gstipcpipelinesink.c',
  'gstipcpipelinesrc.c',
  'gstipcslavepipeline.c'
]

if get_option('ipcpipeline').disabled()
  subdir_done()
endif

have_socket_h = cc.has_header('sys/socket.h')
if not have_socket_h and get_option('ipcpipeline').enabled()
  error('ipcpipeline plugin enabled but socket.h not found')
endif
have_pipe = cc.has_function('pipe')
if not have_pipe and get_option('ipcpipeline').enabled()
  error('ipcpipeline plugin enabled but pipe() not found')
endif
have_socketpair = cc.has_function('socketpair')
if not have_socketpair and get_option('ipcpipeline').enabled()
  error('ipcpipeline plugin enabled but socketpair() not found')
endif

build_ipcpipeline = have_socket_h and have_pipe and have_socketpair
if build_ipcpipeline
  gstipcpipeline = library('gstipcpipeline',
    ipcpipeline_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstipcpipeline, install_dir : plugins_pkgconfig_install_dir)
endif
