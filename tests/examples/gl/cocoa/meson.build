
if host_system == 'darwin'
  executable('cocoa-videooverlay', 'cocoa-videooverlay.m',
    objc_args : [gst_plugins_base_args],
    include_directories: [configinc],
    dependencies : [gstgl_dep,],
    install: false)
endif
