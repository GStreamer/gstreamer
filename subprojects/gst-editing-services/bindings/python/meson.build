env = environment()
env.prepend('_GI_OVERRIDES_PATH', meson.current_source_dir() / 'gi/overrides')
meson.add_devenv(env)

python.install_sources(['gi/overrides/GES.py'],
  pure : false,
  subdir: 'gi/overrides',
)
