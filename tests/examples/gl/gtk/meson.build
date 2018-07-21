extra_args = []
if cc.has_argument ('-Wno-parentheses')
  extra_args += '-Wno-parentheses'
endif

gstgtkhelper = static_library ('gstgtkhelper',
  ['gstgtk.c'],
  c_args : gst_plugins_base_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_base_dep, video_dep, gtk_dep],
  install : false)

gstgtkhelper_dep = declare_dependency(link_with: gstgtkhelper,
  compile_args : extra_args,
  include_directories : include_directories('.'),
  dependencies : [gst_base_dep, video_dep, gtk_dep])

subdir('filternovideooverlay')
subdir('filtervideooverlay')
subdir('fxtest')
subdir('switchvideooverlay')
subdir('3dvideo')
