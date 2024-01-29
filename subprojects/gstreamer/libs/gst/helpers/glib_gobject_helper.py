##
# imported from glib: glib/glib_gdb.py
##
import gdb
import sys

if sys.version_info[0] >= 3:
    long = int

# This is not quite right, as local vars may override symname


def read_global_var(symname):
    return gdb.selected_frame().read_var(symname)


def g_quark_to_string(quark):
    if quark is None:
        return None
    quark = long(quark)
    if quark == 0:
        return None
    max_q = None
    try:
        val = read_global_var("quarks")
        try:
            max_q = long(read_global_var("quark_seq_id"))
        # quark_seq_id gets optimized out in some builds so work around it
        except gdb.error:
            pass
    except Exception:
        try:
            val = read_global_var("g_quarks")
            try:
                max_q = long(read_global_var("g_quark_seq_id"))
            except gdb.error:
                pass
        except Exception:
            return None
    if max_q is None or quark < max_q:
        try:
            return val[quark].string()
        except gdb.MemoryError:
            print("Invalid quark %d" % quark)
    return None

##
# imported from glib: gobject/gobject_gdb.py
##


def is_fundamental(gtype):
    gtype = long(gtype)
    typenode = gtype - gtype % 4

    return typenode < (255 << 2)


def g_type_to_typenode(gtype):
    def lookup_fundamental_type(typenode):
        if typenode == 0:
            return None
        val = read_global_var("static_fundamental_type_nodes")
        if val is None or val.is_optimized_out:
            return None
        return val[typenode >> 2].address

    gtype = long(gtype)
    typenode = gtype - gtype % 4
    if not is_fundamental(gtype):
        res = gdb.Value(typenode).cast(gdb.lookup_type("TypeNode").pointer())
    else:
        res = lookup_fundamental_type(typenode)

    return res


def g_type_fundamental_name(gtype):
    if is_fundamental(gtype):
        return g_type_to_name(gtype)
    else:
        typenode = g_type_to_typenode(gtype)
        if typenode:
            return g_quark_to_string(typenode["qname"])
        return None

    return g_type_to_name(typenode["supers"][int(typenode["n_supers"])])


def g_type_to_name(gtype):
    typenode = g_type_to_typenode(gtype)
    if typenode:
        return g_quark_to_string(typenode["qname"])

    try:
        return gdb.parse_and_eval(f"g_type_name({gtype})").string()
    except Exception:
        return None


def g_type_name_from_instance(instance):
    if long(instance) != 0:
        try:
            inst = instance.cast(gdb.lookup_type("GTypeInstance").pointer())
            klass = inst["g_class"]
            gtype = klass["g_type"]
            name = g_type_to_name(gtype)
            return name
        except RuntimeError:
            pass
    return None
