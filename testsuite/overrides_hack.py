import gi.overrides

if not gi.overrides.__path__[0].endswith("gst-python/gi/overrides"):
    local_overrides = None
    # our overrides don't take precedence, let's fix it
    for i, path in enumerate(gi.overrides.__path__):
        if path.endswith("gst-python/gi/overrides"):
            local_overrides = path

    gi.overrides.__path__.remove(local_overrides)
    gi.overrides.__path__.insert(0, local_overrides)
