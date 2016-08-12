mad_dep = dependency('mad', version : '>= 0.15', required : false)
if not mad_dep.found() and cc.has_header_symbol('mad.h', 'mad_decoder_finish')
  mad_dep = cc.find_library('mad', required : false)
endif

if mad_dep.found()
  mad = library('gstmad',
    ['gstmad.c'],
    c_args : ugly_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, mad_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
endif
