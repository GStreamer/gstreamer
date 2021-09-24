examples = [
    'concatenate',
    'gessrc',
    'simple1',
    'test1',
    'test2',
    'test3',
    'test4',
    'transition',
    'thumbnails',
    'overlays',
    'text_properties',
    'assets',
    'multifilesrc',
    'play_timeline_with_one_clip'
]

# TODO Properly port to Gtk 3
#
# if gtk_dep.found()
#     examples = examples + ['ges-ui']
# endif


foreach example_name : examples
    exe = executable(example_name, '@0@.c'.format(example_name),
        c_args : ges_c_args,
        dependencies : libges_deps + [ges_dep],
    )
endforeach
