subdir('base')
subdir('controller')
subdir('net')
subdir('helpers')

if get_option('check').disabled()
  gst_check_dep = disabler() # KISS for now
else
  subdir('check')
endif
