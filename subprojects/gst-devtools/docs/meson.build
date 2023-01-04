build_hotdoc = false

if meson.is_cross_build()
    if get_option('doc').enabled()
        error('Documentation enabled but building the doc while cross building is not supported yet.')
    endif

    message('Documentation not built as building it while cross building is not supported yet.')
    subdir_done()
endif

if static_build
    if get_option('doc').enabled()
        error('Documentation enabled but not supported when building statically.')
    endif

    message('Building statically, can\'t build the documentation')
    subdir_done()
endif

if not build_gir
    if get_option('doc').enabled()
        error('Documentation enabled but introspection not built.')
    endif

    message('Introspection not built, won\'t build documentation requiring it')
    subdir_done()
endif

hotdoc_p = find_program('hotdoc', required: get_option('doc'))
if not hotdoc_p.found()
    message('Hotdoc not found, not building the documentation')
    subdir_done()
endif

required_hotdoc_extensions = ['gi-extension']
hotdoc = import('hotdoc')
foreach extension: required_hotdoc_extensions
    if not hotdoc.has_extensions(extension)
        if get_option('doc').enabled()
            error('Documentation enabled but @0@ missing'.format(extension))
        endif

        message('@0@ extension not found, not building documentation'.format(extension))
        subdir_done()
    endif
endforeach

excludes = ['gettext.h',
    'gst-validate-internal.h',
    'gst-validate-i18n-lib.c'
]

build_hotdoc = true
validate_excludes = []
foreach f: excludes
    validate_excludes += [join_paths(meson.current_source_dir(), '..',
        'validate', 'gst', 'validate', f)]
endforeach

validate_sources = gstvalidate_headers + gstvalidate_sources
hotdoc = import('hotdoc')
plugins_doc = []
libs_doc = [hotdoc.generate_doc('gst-devtools',
    project_version: apiversion,
    sitemap: 'sitemap.txt',
    index: 'index.md',
    gi_c_sources: validate_sources,
    gi_c_source_filters: validate_excludes,
    gi_index: 'gi-index.md',
    gi_smart_index: true,
    gi_sources: [validate_gir[0].full_path()],
    disable_incremental_build: true,
    dependencies : [validate_dep],
    depends: validate_gir[0],
)]
