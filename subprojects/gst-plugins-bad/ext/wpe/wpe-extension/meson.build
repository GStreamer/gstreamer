library('gstwpeextension',
  ['gstwpeextension.c', 'gstwpeaudiosink.c', 'gstwpebusmsgforwarder.c'],
  dependencies : [wpe_dep, gst_dep, gstbase_dep, giounix_dep],
  c_args : ['-DHAVE_CONFIG_H=1'],
  include_directories : [configinc],
  install : true,
  install_dir : plugins_install_dir / 'wpe-extension')

