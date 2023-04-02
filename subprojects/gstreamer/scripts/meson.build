# dist scripts
if not meson.is_subproject()
  meson.add_dist_script('dist-translations.py', meson.project_version())
endif

# dist common files from monorepo root
if not meson.is_subproject()
  meson.add_dist_script('dist-common-files.py', meson.project_version())
endif
