bash_helper_conf = configuration_data()
bash_helper_conf.set('GST_COMPLETION_HELPER_INSTALLED',
    join_paths(prefix, helpers_install_dir, 'gst-completion-helper'))
bash_helper_conf.set('GST_API_VERSION', apiversion)

configure_file(input : 'gst.in',
    output : 'gst',
    install_dir : bash_helpers_dir,
    configuration : bash_helper_conf)
