
# Some WPE versions (hello 2.38) don't export versioning macros for the WebProcess extension.
wpe_version = wpewebkit_dep.version().split('.')
wpe_version_major = wpe_version[0]
wpe_version_minor = wpe_version[1]
wpe_version_micro = wpe_version[2]

version_c_args = ['-DWPE_VERSION_MAJOR=' + wpe_version_major,
                  '-DWPE_VERSION_MINOR=' + wpe_version_minor,
                  '-DWPE_VERSION_MICRO=' + wpe_version_micro
                 ]

library('gstwpeextension',
  ['gstwpeextension.c', 'gstwpeaudiosink.c', 'gstwpebusmsgforwarder.c'],
  dependencies : [wpewebkit_dep, gst_dep, gstbase_dep, giounix_dep],
  c_args : ['-DHAVE_CONFIG_H=1'] + gst_wpe_c_args + version_c_args,
  include_directories : [configinc],
  install : true,
  install_dir : wpe_extension_install_dir)
