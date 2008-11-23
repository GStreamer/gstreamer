import os

import gobject
import gst

def gst_dump(bin):
    dump_element(bin, 0)

def dump_bin(bin, indent):
        # Iterate the children
        for child in bin.get_list():
            dump_element(child, indent + 2)

def dump_element(element, indent):
        states = { 1: 'NULL', 2: 'READY',
            4: 'PAUSED', 8: 'PLAYING' }

        state = 'UNKNOWN'
        try:
            state = states[element.get_state()]
        except KeyError:
            state = 'UNKNOWN (%d)' % element.get_state()

        c = element.get_clock()
        if c is None:
            clock_str = "clock - None"
        else:
            clock_str = "clock - %s" % (c.get_name())

        out = "%s (%s): state %s, %s" % (element.get_name(),
             gobject.type_name(element.__gtype__), state, clock_str)

        print out.rjust(len(out) + indent)

        tmp = { True: 'active', False: 'inactive' }

        for curpad in element.get_pad_list():
            if curpad.get_direction() == gst.PAD_SRC:
                if curpad.is_linked():
                    peer = curpad.get_peer()
                    out = " - %s:%s (%s) => %s:%s (%s)" % (
                        curpad.get_parent().get_name(), curpad.get_name(),
                        tmp[curpad.is_active()],
                        peer.get_parent().get_name(), peer.get_name(),
                        tmp[peer.is_active()])

                    print out.rjust(len(out) + indent)

        if isinstance(element, gst.Bin):
            dump_bin(element, indent + 2)
        elif isinstance(element, gst.Queue):
            out = " - time_level: %ld" % (
                element.get_property('current-level-time'))
            print out.rjust(len(out) + indent)
            out = " - bytes_level: %ld" % (
                element.get_property('current-level-bytes'))
            print out.rjust(len(out) + indent)

def gc_collect(reason=None):
    """
    Garbage-collect if GST_GC env var is set.
    This helps in debugging object refcounting.
    Sprinkle liberally around checkpoints.
    """
    env = os.environ.get('GST_GC', None)
    if not env:
        return
    import gc
    if env == 'DEBUG_LEAK':
        gc.set_debug(gc.DEBUG_LEAK)

    gst.debug('collecting garbage')
    if reason:
        gst.debug('because of %s' % reason)
    count = gc.collect()
    gst.debug('collected garbage, %d objects collected, %d left' % (
        count, len(gc.get_objects())))
