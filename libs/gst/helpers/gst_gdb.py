# GStreamer
# Copyright (C) 2018 Pengutronix, Michael Olbrich <m.olbrich@pengutronix.de>
#
# gst_gdb.py: gdb extension for GStreamer
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

import gdb
import sys
import re

from glib_gobject_helper import g_type_to_name, g_type_name_from_instance, \
    g_type_to_typenode, g_quark_to_string

if sys.version_info[0] >= 3:
    long = int


def is_gst_type(val, klass):
    def _is_gst_type(type):
        if str(type) == klass:
            return True

        while type.code == gdb.TYPE_CODE_TYPEDEF:
            type = type.target()

        if type.code != gdb.TYPE_CODE_STRUCT:
            return False

        fields = type.fields()
        if len(fields) < 1:
            return False

        first_field = fields[0]
        return _is_gst_type(first_field.type)

    type = val.type
    if type.code != gdb.TYPE_CODE_PTR:
        return False
    type = type.target()
    return _is_gst_type(type)


class GstMiniObjectPrettyPrinter:
    "Prints a GstMiniObject instance pointer"

    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            inst = self.val.cast(gdb.lookup_type("GstMiniObject").pointer())
            gtype = inst["type"]
            name = g_type_to_name(gtype)
            return "0x%x [%s]" % (long(self.val), name)
        except RuntimeError:
            return "0x%x" % long(self.val)


class GstObjectPrettyPrinter:
    "Prints a GstObject instance"

    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            name = g_type_name_from_instance(self.val)
            if not name:
                name = str(self.val.type.target())
            if long(self.val) != 0:
                inst = self.val.cast(gdb.lookup_type("GstObject").pointer())
                inst_name = inst["name"].string()
                if inst_name:
                    name += "|" + inst_name
            return ("0x%x [%s]") % (long(self.val), name)
        except RuntimeError:
            return "0x%x" % long(self.val)


class GstClockTimePrinter:
    "Prints a GstClockTime / GstClockTimeDiff"

    def __init__(self, val):
        self.val = val

    def to_string(self):
        GST_SECOND = 1000000000
        GST_CLOCK_TIME_NONE = 2**64-1
        GST_CLOCK_STIME_NONE = -2**63
        n = int(self.val)
        prefix = ""
        invalid = False
        if str(self.val.type) == "GstClockTimeDiff":
            if n == GST_CLOCK_STIME_NONE:
                invalid = True
            prefix = "+" if n >= 0 else "-"
            n = abs(n)
        else:
            if n == GST_CLOCK_TIME_NONE:
                invalid = True

        if invalid:
            return str(n) + " [99:99:99.999999999]"

        return str(n) + " [%s%u:%02u:%02u.%09u]" % (
            prefix,
            n / (GST_SECOND * 60 * 60),
            (n / (GST_SECOND * 60)) % 60,
            (n / GST_SECOND) % 60,
            n % GST_SECOND)


def gst_pretty_printer_lookup(val):
    if is_gst_type(val, "GstMiniObject"):
        return GstMiniObjectPrettyPrinter(val)
    if is_gst_type(val, "GstObject"):
        return GstObjectPrettyPrinter(val)
    if str(val.type) == "GstClockTime" or str(val.type) == "GstClockTimeDiff":
        return GstClockTimePrinter(val)
    return None


def save_memory_access(fallback):
    def _save_memory_access(func):
        def wrapper(*args, **kwargs):
            try:
                return func(*args, **kwargs)
            except gdb.MemoryError:
                return fallback
        return wrapper
    return _save_memory_access


def save_memory_access_print(message):
    def _save_memory_access_print(func):
        def wrapper(*args, **kwargs):
            try:
                func(*args, **kwargs)
            except gdb.MemoryError:
                _gdb_write(args[1], message)
        return wrapper
    return _save_memory_access_print


def _g_type_from_instance(instance):
    if long(instance) != 0:
        try:
            inst = instance.cast(gdb.lookup_type("GTypeInstance").pointer())
            klass = inst["g_class"]
            gtype = klass["g_type"]
            return gtype
        except RuntimeError:
            pass
    return None


def g_inherits_type(val, typename):
    if is_gst_type(val, "GstObject"):
        gtype = _g_type_from_instance(val)
        if gtype is None:
            return False
        typenode = g_type_to_typenode(gtype)
    elif is_gst_type(val, "GstMiniObject"):
        mini = val.cast(gdb.lookup_type("GstMiniObject").pointer())
        try:
            typenode = mini["type"].cast(gdb.lookup_type("TypeNode").pointer())
        except gdb.MemoryError:
            return False
    else:
        return False

    for i in range(typenode["n_supers"]):
        if g_type_to_name(typenode["supers"][i]) == typename:
            return True
    return False


def gst_is_bin(val):
    return g_inherits_type(val, "GstBin")


def _g_array_iter(array, element_type):
    if array == 0:
        return
    try:
        item = array["data"].cast(element_type.pointer())
        for i in range(int(array["len"])):
            yield item[i]
    except gdb.MemoryError:
        pass


def _g_value_get_value(val):
    typenode = g_type_to_typenode(val["g_type"])
    if not typenode:
        return None
    tname = g_quark_to_string(typenode["qname"])
    fname = g_type_to_name(typenode["supers"][int(typenode["n_supers"])])
    if fname in ("gchar", "guchar", "gboolean", "gint", "guint", "glong",
                 "gulong", "gint64", "guint64", "gfloat", "gdouble",
                 "gpointer", "GFlags"):
        try:
            t = gdb.lookup_type(tname).pointer()
        except RuntimeError:
            t = gdb.lookup_type(fname).pointer()
    elif fname == "gchararray":
        t = gdb.lookup_type("char").pointer().pointer()
    elif fname == "GstBitmask":
        t = gdb.lookup_type("guint64").pointer()
    elif fname == "GstFlagSet":
        t = gdb.lookup_type("guint").pointer().pointer()
        return val["data"].cast(t)
    elif fname == "GstFraction":
        t = gdb.lookup_type("gint").pointer().pointer()
        return val["data"].cast(t)
    elif fname == "GstFractionRange":
        t = gdb.lookup_type("GValue").pointer().pointer()
    elif fname == "GstValueList":
        t = gdb.lookup_type("GArray").pointer().pointer()
    elif fname in ("GBoxed", "GObject"):
        try:
            t = gdb.lookup_type(tname).pointer().pointer()
        except RuntimeError:
            t = gdb.lookup_type(tname).pointer().pointer()
    else:
        return val["data"]

    return val["data"].cast(t).dereference()


def _gdb_write(indent, text):
    gdb.write("%s%s\n" % ("  " * indent, text))


class GdbCapsFeatures:
    def __init__(self, val):
        self.val = val

    def size(self):
        if long(self.val) == 0:
            return 0
        return int(self.val["array"]["len"])

    def items(self):
        if long(self.val) == 0:
            return
        for q in _g_array_iter(self.val["array"], gdb.lookup_type("GQuark")):
            yield q

    def __eq__(self, other):
        if self.size() != other.size():
            return False
        a1 = list(self.items())
        a2 = list(other.items())
        for item in a1:
            if item not in a2:
                return False
        return True

    def __str__(self):
        if long(self.val) == 0:
            return ""
        count = self.size()
        if int(self.val["is_any"]) == 1 and count == 0:
            return "(ANY)"
        if count == 0:
            return ""
        s = ""
        for f in self.items():
            ss = g_quark_to_string(f)
            if ss != "memory:SystemMemory" or count > 1:
                s += ", " if s else ""
                s += ss
        return s


class GdbGstCaps:
    def __init__(self, val):
        self.val = val.cast(gdb.lookup_type("GstCapsImpl").pointer())

    def size(self):
        return int(self.val["array"]["len"])

    def items(self):
        gdb_type = gdb.lookup_type("GstCapsArrayElement")
        for f in _g_array_iter(self.val["array"], gdb_type):
            yield(GdbCapsFeatures(f["features"]),
                  GdbGstStructure(f["structure"]))

    def __eq__(self, other):
        if self.size() != other.size():
            return False
        a1 = list(self.items())
        a2 = list(other.items())
        for i in range(self.size()):
            if a1[i] != a2[i]:
                return False
        return True

    def dot(self):
        if self.size() == 0:
            return "ANY"
        s = ""
        for (features, structure) in self.items():
            s += structure.name()
            tmp = str(features)
            if tmp:
                s += "(" + tmp + ")"
            s += "\\l"
            if structure.size() > 0:
                s += "\\l".join(structure.value_strings("  %18s: %s")) + "\\l"
        return s

    @save_memory_access_print("<inaccessible memory>")
    def print(self, indent, prefix=""):
        items = list(self.items())
        if len(items) != 1:
            _gdb_write(indent, prefix)
            prefix = ""
        for (features, structure) in items:
            s = "%s %s" % (prefix, structure.name())
            tmp = str(features)
            if tmp:
                s += "(" + tmp + ")"
            _gdb_write(indent, s)
            for val in structure.value_strings("%s: %s", False):
                _gdb_write(indent+1, val)
        return s


class GdbGValue:
    def __init__(self, val):
        self.val = val

    def fundamental_typename(self):
        typenode = g_type_to_typenode(self.val["g_type"])
        if not typenode:
            return None
        return g_type_to_name(typenode["supers"][int(typenode["n_supers"])])

    def value(self):
        return _g_value_get_value(self.val)

    def __str__(self):
        try:
            value = self.value()
            tname = self.fundamental_typename()
            gvalue_type = gdb.lookup_type("GValue")
            if tname == "GstFraction":
                v = "%d/%d" % (value[0], value[1])
            elif tname == "GstBitmask":
                v = "0x%016x" % long(value)
            elif tname == "gboolean":
                v = "false" if int(value) == 0 else "true"
            elif tname == "GstFlagSet":
                v = "%x:%x" % (value[0], value[1])
            elif tname == "GstIntRange":
                rmin = int(value[0]["v_uint64"]) >> 32
                rmax = int(value[0]["v_uint64"]) & 0xffffffff
                step = int(value[1]["v_int"])
                if step == 1:
                    v = "[ %d, %d ]" % (rmin, rmax)
                else:
                    v = "[ %d, %d, %d ]" % (rmin*step, rmax*step, step)
            elif tname == "GstFractionRange":
                v = "[ %s, %s ]" % (GdbGValue(value[0]), GdbGValue(value[1]))
            elif tname in ("GstValueList", "GstValueArray"):
                if gvalue_type.fields()[1].type == value.type:
                    gdb_type = gdb.lookup_type("GArray").pointer()
                    value = value[0]["v_pointer"].cast(gdb_type)
                v = "<"
                for l in _g_array_iter(value, gvalue_type):
                    v += " " if v == "<" else ", "
                    v += str(GdbGValue(l))
                v += " >"
            else:
                try:
                    v = value.string()
                except RuntimeError:
                    # it is not a string-like type
                    if gvalue_type.fields()[1].type == value.type:
                        # don't print the raw GValue union
                        v = "<unkown type: %s>" % tname
                    else:
                        v = str(value)
        except gdb.MemoryError:
            v = "<inaccessible memory at 0x%x>" % int(self.val)
        return v

    def __eq__(self, other):
        return self.val == other.val


class GdbGstStructure:
    def __init__(self, val):
        self.val = val.cast(gdb.lookup_type("GstStructureImpl").pointer())

    @save_memory_access("<inaccessible memory>")
    def name(self):
        return g_quark_to_string(self.val["s"]["name"])

    @save_memory_access(0)
    def size(self):
        return int(self.val["fields"]["len"])

    def values(self):
        for f in _g_array_iter(self.val["fields"],
                               gdb.lookup_type("GstStructureField")):
            key = g_quark_to_string(f["name"])
            value = GdbGValue(f["value"])
            yield(key, value)

    def value(self, key):
        for (k, value) in self.values():
            if k == key:
                return value
        raise KeyError(key)

    def __eq__(self, other):
        if self.size() != other.size():
            return False
        a1 = list(self.values())
        a2 = list(other.values())
        for (key, value) in a1:
            if (key, value) not in a2:
                return False
        return True

    def value_strings(self, pattern, elide=True):
        s = []
        for (key, value) in self.values():
            v = str(value)
            if elide and len(v) > 25:
                if v[0] in "[(<\"":
                    v = v[:20] + "... " + v[-1:]
                else:
                    v = v[:22] + "..."
            s.append(pattern % (key, v))
        return s

    @save_memory_access_print("<inaccessible memory>")
    def print(self, indent, prefix=None):
        if prefix is not None:
            _gdb_write(indent, "%s: %s" % (prefix, self.name()))
        else:
            _gdb_write(indent, "%s:" % (prefix, self.name()))
        for (key, value) in self.values():
            _gdb_write(indent+1, "%s: %s" % (key, str(value)))


class GdbGstEvent:
    def __init__(self, val):
        self.val = val.cast(gdb.lookup_type("GstEventImpl").pointer())

    @save_memory_access("<inaccessible memory>")
    def typestr(self):
        t = self.val["event"]["type"]
        (event_quarks, _) = gdb.lookup_symbol("event_quarks")
        event_quarks = event_quarks.value()
        i = 0
        while event_quarks[i]["name"] != 0:
            if t == event_quarks[i]["type"]:
                return event_quarks[i]["name"].string()
            i += 1
        return None

    def structure(self):
        return GdbGstStructure(self.val["structure"])

    @save_memory_access_print("<inaccessible memory>")
    def print(self, indent):
        typestr = self.typestr()
        if typestr == "caps":
            caps = GdbGstCaps(self.structure().value("caps").value())
            caps.print(indent, "caps:")
        elif typestr == "stream-start":
            stream_id = self.structure().value("stream-id").value()
            _gdb_write(indent, "stream-start:")
            _gdb_write(indent + 1, "stream-id: %s" % stream_id.string())
        elif typestr == "segment":
            segment = self.structure().value("segment").value()
            fmt = str(segment["format"]).split("_")[-1].lower()
            _gdb_write(indent, "segment: %s" % fmt)
            rate = float(segment["rate"])
            applied_rate = float(segment["applied_rate"])
            if applied_rate != 1.0:
                applied = "(applied rate: %g)" % applied_rate
            else:
                applied = ""
            _gdb_write(indent+1, "rate: %g%s" % (rate, applied))
        elif typestr == "tag":
            struct = self.structure()
            # skip 'GstTagList-'
            name = struct.name()[11:]
            t = gdb.lookup_type("GstTagListImpl").pointer()
            s = struct.value("taglist").value().cast(t)["structure"]
            structure = GdbGstStructure(s)
            _gdb_write(indent, "tag: %s" % name)
            for (key, value) in structure.values():
                _gdb_write(indent+1, "%s: %s" % (key, str(value)))
        else:
            self.structure().print(indent, typestr)


class GdbGstObject:
    def __init__(self, klass, val):
        self.val = val.cast(klass)

    @save_memory_access("<inaccessible memory>")
    def name(self):
        obj = self.val.cast(gdb.lookup_type("GstObject").pointer())
        return obj["name"].string()

    def dot_name(self):
        ptr = self.val.cast(gdb.lookup_type("void").pointer())
        return re.sub('[^a-zA-Z0-9<>]', '_', "%s_%s" % (self.name(), str(ptr)))

    def parent(self):
        obj = self.val.cast(gdb.lookup_type("GstObject").pointer())
        return obj["parent"]

    def parent_element(self):
        p = self.parent()
        if p != 0 and g_inherits_type(p, "GstElement"):
            element = p.cast(gdb.lookup_type("GstElement").pointer())
            return GdbGstElement(element)
        return None


class GdbGstPad(GdbGstObject):
    def __init__(self, val):
        gdb_type = gdb.lookup_type("GstPad").pointer()
        super(GdbGstPad, self).__init__(gdb_type, val)

    def __eq__(self, other):
        return self.val == other.val

    def is_linked(self):
        return long(self.val["peer"]) != 0

    def peer(self):
        return GdbGstPad(self.val["peer"])

    def direction(self):
        return str(self.val["direction"])

    def events(self):
        if long(self.val["priv"]) == 0:
            return
        array = self.val["priv"]["events"]
        for ev in _g_array_iter(array, gdb.lookup_type("PadEvent")):
            yield GdbGstEvent(ev["event"])

    def caps(self):
        for ev in self.events():
            if ev.typestr() != "caps":
                continue
            return GdbGstCaps(ev.structure().value("caps").value())
        return None

    def template_caps(self):
        tmp = self.val["padtemplate"]
        return GdbGstCaps(tmp["caps"]) if int(tmp) != 0 else None

    def mode(self):
        m = str(self.val["mode"]).split("_")[-1].lower()
        if m in ("push", "pull"):
            return m
        return None

    def pad_type(self):
        s = str(self.val["direction"]).split("_")[-1].capitalize()
        if g_inherits_type(self.val, "GstGhostPad"):
            s += "Ghost"
        return s + "Pad"

    @save_memory_access_print("Pad(<inaccessible memory>)")
    def print(self, indent):
        m = ", " + self.mode() if self.mode() else ""
        _gdb_write(indent, "%s(%s%s) {" % (self.pad_type(), self.name(), m))
        first = True
        for ev in self.events():
            if first:
                _gdb_write(indent+1, "events:")
                first = False
            ev.print(indent+2)
        _gdb_write(indent, "}")

    def _dot(self, color, pname, indent):
        spc = "  " * indent
        activation_mode = "-><"
        style = "filled,solid"
        template = self.val["padtemplate"]
        if template != 0:
            presence = template["presence"]
            if str(presence) == "GST_PAD_SOMETIMES":
                style = "filled,dotted"
            if str(presence) == "GST_PAD_REQUEST":
                style = "filled,dashed"
        task_mode = ""
        task = self.val["task"]
        if long(task) != 0:
            task_state = int(task["state"])
            if task_state == 0:  # started
                task_mode = "[T]"
            if task_state == 2:  # paused
                task_mode = "[t]"
        f = int(self.val["object"]["flags"])
        flags = "B" if f & 16 else "b"  # GST_PAD_FLAG_BLOCKED
        flags += "F" if f & 32 else "f"  # GST_PAD_FLAG_FLUSHING
        flags += "B" if f & 16 else "b"  # GST_PAD_FLAG_BLOCKING

        s = "%s  %s_%s [color=black, fillcolor=\"%s\", " \
            "label=\"%s%s\\n[%c][%s]%s\", height=\"0.2\", style=\"%s\"];\n" % \
            (spc, pname, self.dot_name(), color, self.name(), "",
             activation_mode[int(self.val["mode"])], flags, task_mode, style)
        return s

    def dot(self, indent):
        spc = "  " * indent
        direction = self.direction()
        element = self.parent_element()
        ename = element.dot_name() if element else ""
        s = ""
        if g_inherits_type(self.val, "GstGhostPad"):
            if direction == "GST_PAD_SRC":
                color = "#ffdddd"
            elif direction == "GST_PAD_SINK":
                color = "#ddddff"
            else:
                color = "#ffffff"

            t = gdb.lookup_type("GstProxyPad").pointer()
            other = GdbGstPad(self.val.cast(t)["priv"]["internal"])
            if other:
                s += other._dot(color, "", indent)
                pname = self.dot_name()
                other_element = other.parent_element()
                other_ename = other_element.dot_name() if other_element else ""
                other_pname = other.dot_name()
                if direction == "GST_PAD_SRC":
                    s += "%s%s_%s -> %s_%s [style=dashed, minlen=0]\n" % \
                       (spc, other_ename, other_pname, ename, pname)
                else:
                    s += "%s%s_%s -> %s_%s [style=dashed, minlen=0]\n" % \
                       (spc, ename, pname, other_ename, other_pname)
        else:
            if direction == "GST_PAD_SRC":
                color = "#ffaaaa"
            elif direction == "GST_PAD_SINK":
                color = "#aaaaff"
            else:
                color = "#cccccc"

        s += self._dot(color, ename, indent)
        return s

    def link_dot(self, indent, element):
        spc = "  " * indent

        peer = self.peer()
        peer_element = peer.parent_element()

        caps = self.caps()
        if not caps:
            caps = self.template_caps()
        peer_caps = peer.caps()
        if not peer_caps:
            peer_caps = peer.template_caps()

        pname = self.dot_name()
        ename = element.dot_name() if element else ""
        peer_pname = peer.dot_name()
        peer_ename = peer_element.dot_name() if peer_element else ""

        if caps and peer_caps and caps == peer_caps:
            s = "%s%s_%s -> %s_%s [label=\"%s\"]\n" % \
               (spc, ename, pname, peer_ename, peer_pname, caps.dot())
        elif caps and peer_caps and caps != peer_caps:
            s = "%s%s_%s -> %s_%s [labeldistance=\"10\", labelangle=\"0\", " \
                % (spc, ename, pname, peer_ename, peer_pname)
            s += "label=\"" + " "*50 + "\", "
            if self.direction() == "GST_PAD_SRC":
                media_src = caps.dot()
                media_dst = peer_caps.dot()
            else:
                media_src = peer_caps.dot()
                media_dst = caps.dot()
            s += "taillabel=\"%s\", headlabel=\"%s\"]\n" % \
                 (media_src, media_dst)
        else:
            s = "%s%s_%s -> %s_%s\n" % \
                (spc, ename, pname, peer_ename, peer_pname)
        return s


class GdbGstElement(GdbGstObject):
    def __init__(self, val):
        gdb_type = gdb.lookup_type("GstElement").pointer()
        super(GdbGstElement, self).__init__(gdb_type, val)
        self.is_bin = gst_is_bin(self.val)

    def __eq__(self, other):
        return self.val == other.val

    def children(self):
        if not self.is_bin:
            return
        b = self.val.cast(gdb.lookup_type("GstBin").pointer())
        link = b["children"]
        while link != 0:
            yield GdbGstElement(link["data"])
            link = link["next"]

    def has_pads(self, pad_group="pads"):
        return self.val[pad_group] != 0

    def pads(self, pad_group="pads"):
        link = self.val[pad_group]
        while link != 0:
            yield GdbGstPad(link["data"])
            link = link["next"]

    def _state_dot(self):
        icons = "~0-=>"
        current = int(self.val["current_state"])
        pending = int(self.val["pending_state"])
        if pending == 0:
            # GST_ELEMENT_FLAG_LOCKED_STATE == 16
            locked = (int(self.val["object"]["flags"]) & 16) != 0
            return "\\n[%c]%s" % (icons[current], "(locked)" if locked else "")
        return "\\n[%c] -> [%c]" % (icons[current], icons[pending])

    @save_memory_access_print("Element(<inaccessible memory>)")
    def print(self, indent):
        _gdb_write(indent, "%s(%s) {" %
                   (g_type_name_from_instance(self.val), self.name()))
        for p in self.pads():
            p.print(indent+2)
        _gdb_write(indent, "}")

    def _dot(self, indent=0):
        spc = "  " * indent

        s = "%ssubgraph cluster_%s {\n" % (spc, self.dot_name())
        s += "%s  fontname=\"Bitstream Vera Sans\";\n" % spc
        s += "%s  fontsize=\"8\";\n" % spc
        s += "%s  style=\"filled,rounded\";\n" % spc
        s += "%s  color=black;\n" % spc
        s += "%s  label=\"%s\\n%s%s%s\";\n" % \
             (spc, g_type_name_from_instance(self.val), self.name(),
              self._state_dot(), "")

        sink_name = None
        if self.has_pads("sinkpads"):
            (ss, sink_name) = self._dot_pads(indent+1, "sinkpads",
                                             self.dot_name() + "_sink")
            s += ss
        src_name = None
        if self.has_pads("srcpads"):
            (ss, src_name) = self._dot_pads(indent+1, "srcpads",
                                            self.dot_name() + "_src")
            s += ss
        if sink_name and src_name:
            name = self.dot_name()
            s += "%s  %s_%s -> %s_%s [style=\"invis\"];\n" % \
                 (spc, name, sink_name, name, src_name)

        if gst_is_bin(self.val):
            s += "%s  fillcolor=\"#ffffff\";\n" % spc
            s += self.dot(indent+1)
        else:
            if src_name and not sink_name:
                s += "%s  fillcolor=\"#ffaaaa\";\n" % spc
            elif not src_name and sink_name:
                s += "%s  fillcolor=\"#aaaaff\";\n" % spc
            elif src_name and sink_name:
                s += "%s  fillcolor=\"#aaffaa\";\n" % spc
            else:
                s += "%s  fillcolor=\"#ffffff\";\n" % spc
        s += "%s}\n\n" % spc

        for p in self.pads():
            if not p.is_linked():
                continue
            if p.direction() == "GST_PAD_SRC":
                s += p.link_dot(indent, self)
            else:
                pp = p.peer()
                if not g_inherits_type(pp.val, "GstGhostPad") and \
                   g_inherits_type(pp.val, "GstProxyPad"):
                    s += pp.link_dot(indent, None)
        return s

    def _dot_pads(self, indent, pad_group, cluster_name):
        spc = "  " * indent
        s = "%ssubgraph cluster_%s {\n" % (spc, cluster_name)
        s += "%s  label=\"\";\n" % spc
        s += "%s  style=\"invis\";\n" % spc
        name = None
        for p in self.pads(pad_group):
            s += p.dot(indent)
            if not name:
                name = p.dot_name()
        s += "%s}\n\n" % spc
        return(s, name)

    def dot(self, indent):
        s = ""
        for child in self.children():
            try:
                s += child._dot(indent)
            except gdb.MemoryError:
                gdb.write("warning: inaccessible memory in element 0x%x\n" %
                          long(child.val))
        return s

    def pipeline_dot(self):
        t = g_type_name_from_instance(self.val)

        s = "digraph pipeline {\n"
        s += "  rankdir=LR;\n"
        s += "  fontname=\"sans\";\n"
        s += "  fontsize=\"10\";\n"
        s += "  labelloc=t;\n"
        s += "  nodesep=.1;\n"
        s += "  ranksep=.2;\n"
        s += "  label=\"<%s>\\n%s%s%s\";\n" % (t, self.name(), "", "")
        s += "  node [style=\"filled,rounded\", shape=box, fontsize=\"9\", " \
             "fontname=\"sans\", margin=\"0.0,0.0\"];\n"
        s += "  edge [labelfontsize=\"6\", fontsize=\"9\", " \
             "fontname=\"monospace\"];\n"
        s += "  \n"
        s += "  legend [\n"
        s += "    pos=\"0,0!\",\n"
        s += "    margin=\"0.05,0.05\",\n"
        s += "    style=\"filled\",\n"
        s += "    label=\"Legend\\lElement-States: [~] void-pending, " \
             "[0] null, [-] ready, [=] paused, [>] playing\\l" \
             "Pad-Activation: [-] none, [>] push, [<] pull\\l" \
             "Pad-Flags: [b]locked, [f]lushing, [b]locking, [E]OS; " \
             "upper-case is set\\lPad-Task: [T] has started task, " \
             "[t] has paused task\\l\",\n"
        s += "  ];"
        s += "\n"

        s += self.dot(1)

        s += "}\n"

        return s


class GstDot(gdb.Command):
    """\
Create a pipeline dot file as close as possible to the output of
GST_DEBUG_BIN_TO_DOT_FILE. This command will find the top-level parent
for the given gstreamer object and create the dot for that element.

Usage: gst-dot <gst-object> <file-name>"""
    def __init__(self):
        super(GstDot, self).__init__("gst-dot", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        self.dont_repeat()
        args = gdb.string_to_argv(arg)
        if len(args) != 2:
            raise Exception("Usage: gst-dot <gst-object> <file>")

        value = gdb.parse_and_eval(args[0])
        if not value:
            raise Exception("'%s' is not a valid object" % args[0])

        if value.type.code != gdb.TYPE_CODE_PTR:
            value = value.address

        if not is_gst_type(value, "GstObject"):
            raise Exception("'%s' is not a GstObject" % args[0])

        value = value.cast(gdb.lookup_type("GstObject").pointer())
        try:
            while value["parent"] != 0:
                tmp = value["parent"]
                # sanity checks to handle memory corruption
                if g_inherits_type(value, "GstElement") and \
                   GdbGstElement(value) not in GdbGstElement(tmp).children():
                    break
                if g_inherits_type(value, "GstPad") and \
                   GdbGstPad(value) not in GdbGstElement(tmp).pads():
                    break
                value = tmp
        except gdb.MemoryError:
            pass

        if not g_inherits_type(value, "GstElement"):
            raise Exception("Toplevel parent is not a GstElement")
        value = value.cast(gdb.lookup_type("GstElement").pointer())

        dot = GdbGstElement(value).pipeline_dot()
        file = open(args[1], "w")
        file.write(dot)
        file.close()

    def complete(self, text, word):
        cmd = gdb.string_to_argv(text)
        if len(cmd) == 0 or(len(cmd) == 1 and len(word) > 0):
            return gdb.COMPLETE_SYMBOL
        return gdb.COMPLETE_FILENAME


class GstPrint(gdb.Command):
    """\
Print high-level information for GStreamer objects

Usage gst-print <gstreamer-object>"""
    def __init__(self):
        super(GstPrint, self).__init__("gst-print", gdb.COMMAND_DATA,
                                       gdb.COMPLETE_SYMBOL)

    def invoke(self, arg, from_tty):
        value = gdb.parse_and_eval(arg)
        if not value:
            raise Exception("'%s' is not a valid object" % args[0])

        if value.type.code != gdb.TYPE_CODE_PTR:
            value = value.address

        if g_inherits_type(value, "GstElement"):
            obj = GdbGstElement(value)
        elif g_inherits_type(value, "GstPad"):
            obj = GdbGstPad(value)
        elif g_inherits_type(value, "GstCaps"):
            obj = GdbGstCaps(value)
        elif g_inherits_type(value, "GstEvent"):
            obj = GdbGstCaps(value)
        else:
            raise Exception("'%s' has an unkown type" % arg)

        obj.print(0)


GstDot()
GstPrint()


def register(obj):
    if obj is None:
        obj = gdb

    # Make sure this is always used befor the glib lookup function.
    # Otherwise the gobject pretty printer is used for GstObjects
    obj.pretty_printers.insert(0, gst_pretty_printer_lookup)
