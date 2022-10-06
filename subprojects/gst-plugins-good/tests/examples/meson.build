subdir('audiofx')
subdir('cairo')
subdir('level')
subdir('qt')
subdir('qt6')

if is_variable('gstrpicamsrc')
  subdir('rpicamsrc')
endif

subdir('rtp')
subdir('rtsp')
subdir('shapewipe')
if have_v4l2
  subdir('v4l2')
endif

if gtk_dep.found()
  subdir('equalizer')
  subdir('jack')
  subdir('spectrum')
  subdir('gtk')
endif

