import gdb
import sys
import re

from glib_gobject_helper import g_type_to_name, g_type_name_from_instance

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


def register(obj):
    if obj is None:
        obj = gdb

    # Make sure this is always used befor the glib lookup function.
    # Otherwise the gobject pretty printer is used for GstObjects
    obj.pretty_printers.insert(0, gst_pretty_printer_lookup)
