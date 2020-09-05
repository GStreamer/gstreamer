if (bashcomp_found)
  subdir('bash-completion/helpers')
  install_data('bash-completion/completions/gst-launch-1.0',
    install_dir : bash_completions_dir,
    install_tag : 'runtime')
  install_data('bash-completion/completions/gst-inspect-1.0',
    install_dir : bash_completions_dir,
    install_tag : 'runtime')
endif

if host_system == 'android'
  install_data('android/GStreamer.java', install_dir: get_option('datadir') / 'gst-android/ndk-build')
endif
