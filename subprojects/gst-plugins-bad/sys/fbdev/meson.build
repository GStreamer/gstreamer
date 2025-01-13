fbdevsink_sources = [
  'gstfbdevsink.c',
]

fbdevsink_headers = [
  'gstfbdevsink.h',
]

doc_sources = []
foreach s: fbdevsink_sources + fbdevsink_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'fbdevsink': pathsep.join(doc_sources)
}

if get_option('fbdev').disabled()
  subdir_done()
endif

have_fb_h = cc.has_header('linux/fb.h')
if not have_fb_h and get_option('fbdev').enabled()
  error('fbdev plugin enabled but fb.h not found')
endif

if have_fb_h
  gstfbdevsink = library('gstfbdevsink',
    fbdevsink_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstfbdevsink]
endif
