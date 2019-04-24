project('GStreamer manuals and tutorials', 'c',
  version: '1.17.0.1',
  meson_version: '>=0.48.0')

hotdoc = import('hotdoc')

apiversion = '1.0'

html_theme = 'https://github.com/hotdoc/hotdoc_lumen_theme/releases/download/0.9/hotdoc_lumen_theme-0.9.tar.xz?sha256=ec3807f13eda2d4914fcf251b7e9607e90f33430725a2b1d433a90c9210a52fc'

gstreamer_doc = hotdoc.generate_doc('GStreamer',
    project_version: apiversion,
    sitemap: 'sitemap.txt',
    index: 'markdown/index.md',
    build_by_default: true,
    install: true,
    extra_assets: [join_paths(meson.current_source_dir(), 'images')],
    syntax_highlighting_activate: true,
    html_theme: html_theme,
    include_paths: join_paths(meson.current_source_dir(), 'examples'),
    html_extra_theme: join_paths(meson.current_source_dir(), 'theme/extra'),
    edit_on_github_repository: 'https://gitlab.freedesktop.org/gstreamer/gst-docs/',
    edit_on_github_branch: 'master',
    disable_incremental_build: true,
    devhelp_activate: true,
)
