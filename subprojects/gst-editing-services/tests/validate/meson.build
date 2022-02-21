subdir ('scenarios')

install_data (['geslaunch.py'],
    install_dir : join_paths(get_option('libdir'),
      'gst-validate-launcher', 'python', 'launcher', 'apps'))

env = environment()
env.prepend('GST_VALIDATE_APPS_DIR', meson.current_source_dir())
meson.add_devenv(env)
