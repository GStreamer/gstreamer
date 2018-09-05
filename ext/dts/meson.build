dca_dep = dependency('libdca', required : false)

if not dca_dep.found() and cc.has_header_symbol('dca.h', 'dca_init')
  dca_dep = cc.find_library('dca', required : false)
endif

no_warn_c_args = []
if cc.get_id() != 'msvc'
  # autotools didn't use the libdca pkg-config cflags, and they
  # can point to a non-existing location (/usr/include/dca)
  no_warn_c_args = ['-Wno-missing-include-dirs']
endif

if dca_dep.found()
  gstdtsdec = library('gstdtsdec',
    'gstdtsdec.c',
    c_args : gst_plugins_bad_args + no_warn_c_args,
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstaudio_dep, orc_dep, dca_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
