ges_excludes = []
foreach f: ['gesmarshal.*',
            'ges-internal.*',
            'ges-auto-transition.*',
            'ges-structured-interface.*',
            'ges-structure-parser.*',
            'ges-smart-*.',
            'gstframepositioner.*',
            'lex.priv_ges_parse_yy.c']
    ges_excludes += [join_paths(meson.current_source_dir(), '..', '..', 'ges', f)]
endforeach

hotdoc = import('hotdoc')
gst_editing_services_doc = hotdoc.generate_doc('gst-editing-services',
    project_version: apiversion,
    extra_assets: [join_paths(meson.current_source_dir(), 'images')],
    c_sources: ['../../ges/ges.h', '../../ges/*.[hc]'],
    gi_sources: [ges_gir[0].full_path()],
    c_source_filters: ges_excludes,
    sitemap: 'sitemap.txt',
    index: 'index.md',
    gi_index: 'index.md',
    gi_smart_index: true,
    gi_order_generated_subpages: true,
    dependencies: [ges_dep],
    disable_incremental_build: true,
)
