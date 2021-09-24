if get_option('a52dec').disabled()
  subdir_done()
endif

a52_dep = cc.find_library('a52', required : get_option('a52dec'))
have_a52_h = cc.has_header_symbol('a52dec/a52.h', 'a52_init', prefix : '#include <stdint.h>')
if not have_a52_h and get_option('a52dec').enabled()
  error('a52dec plugin enabled but a52.h not found')
endif

if a52_dep.found() and have_a52_h
  a52dec = library('gsta52dec',
    'gsta52dec.c',
    c_args : ugly_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, orc_dep, a52_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(a52dec, install_dir : plugins_pkgconfig_install_dir)
  plugins += [a52dec]
endif
