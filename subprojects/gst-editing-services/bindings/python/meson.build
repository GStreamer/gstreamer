install_data(['gi/overrides/GES.py'], install_dir: pygi_override_dir)

env = environment()
env.prepend('_GI_OVERRIDES_PATH', meson.current_source_dir() / 'gi/overrides')
meson.add_devenv(env)
