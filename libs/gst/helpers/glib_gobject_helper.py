##
## imported from glib: glib/glib_gdb.py
##
import gdb
import sys

if sys.version_info[0] >= 3:
    long = int

# This is not quite right, as local vars may override symname
def read_global_var (symname):
    return gdb.selected_frame().read_var(symname)

def g_quark_to_string (quark):
    if quark is None:
        return None
    quark = long(quark)
    if quark == 0:
        return None
    try:
        val = read_global_var ("quarks")
        max_q = long(read_global_var ("quark_seq_id"))
    except:
        try:
            val = read_global_var ("g_quarks")
            max_q = long(read_global_var ("g_quark_seq_id"))
        except:
            return None;
    if quark < max_q:
        return val[quark].string()
    return None

##
## imported from glib: gobject/gobject_gdb.py
##

def g_type_to_typenode (gtype):
    def lookup_fundamental_type (typenode):
        if typenode == 0:
            return None
        val = read_global_var ("static_fundamental_type_nodes")
        if val is None:
            return None
        return val[typenode >> 2].address

    gtype = long(gtype)
    typenode = gtype - gtype % 4
    if typenode > (255 << 2):
        typenode = gdb.Value(typenode).cast (gdb.lookup_type("TypeNode").pointer())
    else:
        typenode = lookup_fundamental_type (typenode)
    return typenode

def g_type_to_name (gtype):
    typenode = g_type_to_typenode(gtype)
    if typenode != None:
        return g_quark_to_string (typenode["qname"])
    return None

def g_type_name_from_instance (instance):
    if long(instance) != 0:
        try:
            inst = instance.cast (gdb.lookup_type("GTypeInstance").pointer())
            klass = inst["g_class"]
            gtype = klass["g_type"]
            name = g_type_to_name (gtype)
            return name
        except RuntimeError:
            pass
    return None
