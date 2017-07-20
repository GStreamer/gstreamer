d3dvideosink_sources = [
  'd3dhelpers.c',
  'd3dvideosink.c',
]

if host_system == 'windows' and cc.has_header('d3d9.h')
  d3dvideosink_dep = [cc.find_library('d3d9'), cc.find_library('gdi32')]

  gstd3dvideosink = library('gstd3d',
    d3dvideosink_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep] + d3dvideosink_dep,
    install : true,
    install_dir : plugins_install_dir)
endif
