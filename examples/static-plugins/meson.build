project('myapp', 'c')

dep = dependency('gstreamer-full-1.0')
app = executable('myapp', 'main.c', dependencies : dep)

test('test-gstreamer-full', app)
