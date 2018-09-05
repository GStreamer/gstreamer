subdir('base')
subdir('controller')
subdir('net')
subdir('helpers')

if get_option('check').disabled()
  gst_check_dep = dependency('', required : false)
else
  subdir('check')
endif
