winscreencap_sources = [
  'gstdx9screencapsrc.c',
  'gstgdiscreencapsrc.c',
  'gstwinscreencap.c',
]

if host_system == 'windows' and cc.has_header('d3d9.h')
  winscreencap_dep = [cc.find_library('d3d9'), cc.find_library('gdi32')]

  gstwinscreencap = library('gstwinscreencap',
    winscreencap_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep] + winscreencap_dep,
    install : true,
    install_dir : plugins_install_dir,
  )
endif
