pangocairo_dep = dependency('pangocairo', version : '>= 1.22.0',
                            required : get_option('closedcaption'))

zvbi_sources = [
  'bit_slicer.c',
  'decoder.c',
  'raw_decoder.c',
  'sampling_par.c',
]

if pangocairo_dep.found()
  gstclosedcaption = library('gstclosedcaption',
    'gstcccombiner.c', 'gstccextractor.c', 'gstccconverter.c', 'gstclosedcaption.c',
    'gstline21dec.c', 'gstcea708decoder.c', 'gstceaccoverlay.c', zvbi_sources,
    c_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstvideo_dep, gstbase_dep, gst_dep, pangocairo_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstclosedcaption, install_dir : plugins_pkgconfig_install_dir)
endif
