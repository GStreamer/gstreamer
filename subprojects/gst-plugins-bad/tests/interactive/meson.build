if not get_option('soundtouch').disabled() and soundtouch_dep.found()
  executable('pitch-test', 'pitch-test.c',
    include_directories: [configinc],
    dependencies: [gst_dep, gstcontroller_dep],
    install: false)

  configure_file(input: '../files/audio-8s-then-reverse.ogg',
    output: 'audio-8s-then-reverse.ogg',
    copy: true)
  executable('pitch-playback-test', 'pitch-playback-test.c',
    include_directories: [configinc],
    dependencies: [gst_dep],
    install: false)
endif
