import os
import gi.overrides

LOCAL_OVERRIDE_PATH = "gst-editing-services/bindings/python/gi/overrides/"
FILE = os.path.realpath(__file__)
if not gi.overrides.__path__[0].endswith(LOCAL_OVERRIDE_PATH):
    local_overrides = None
    # our overrides don't take precedence, let's fix it
    for i, path in enumerate(gi.overrides.__path__):
        if path.endswith(LOCAL_OVERRIDE_PATH):
            local_overrides = path

    if local_overrides:
        gi.overrides.__path__.remove(local_overrides)
    else:
        local_overrides = os.path.abspath(os.path.join(FILE, "../../../../../", LOCAL_OVERRIDE_PATH))

    gi.overrides.__path__.insert(0, local_overrides)

# Execute previously set sitecustomize.py script if it existed
if os.environ.get("GST_ENV"):
    old_sitecustomize = os.path.join(os.path.dirname(__file__),
                                    "old.sitecustomize.gstuninstalled.py")
    if os.path.exists(old_sitecustomize):
        exec(compile(open(old_sitecustomize).read(), old_sitecustomize, 'exec'))
