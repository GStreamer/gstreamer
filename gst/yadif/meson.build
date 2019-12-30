yadif_sources = [
  'gstyadif.c',
  'vf_yadif.c',
  'yadif.c'
]

local_c_args = gst_plugins_bad_args

# The plugin can build asm extensions
# on x86_64
if host_machine.cpu() == 'x86_64'
  local_c_args += ['-DHAVE_CPU_X86_64=1']
endif

gstyadif = library('gstyadif',
  yadif_sources,
  c_args : local_c_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstyadif, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstyadif]
