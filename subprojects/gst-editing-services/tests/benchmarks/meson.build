ges_benchmarks = ['timeline']

foreach b : ges_benchmarks
    fname = '@0@.c'.format(b)
    executable('benchmark-' + b, fname,
        c_args : ges_c_args,
        include_directories : [configinc],
        dependencies : ges_dep
    )
endforeach
