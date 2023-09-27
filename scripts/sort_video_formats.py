#!/usr/bin/env python3
import argparse
import gi
import os
import sys

gi.require_version("Gst", "1.0")
gi.require_version("GstVideo", "1.0")

from gi.repository import GLib
from gi.repository import Gst, GstVideo


def video_format_exists(f):
    return GstVideo.video_format_from_string(f) != GstVideo.VideoFormat.UNKNOWN


def video_format_info_from_string(s):
    f = GstVideo.video_format_from_string(s)
    return GstVideo.video_format_get_info(f)


def parse_format_from_c(token, filename):
    code = open(filename).read().splitlines()
    token_found = False
    formats = None

    for idx, line in enumerate(code):
        if formats is None:
            match = f"#define {token}"
            if line.startswith(match):
                formats = []
                line = line[len(match):]
            else:
                continue

        tmp = line.replace("\\", "")\
                  .replace("\"", "")\
                  .replace(",", " ")\
                  .replace("{", " ")\
                  .replace("}", " ")\
                  .split()
        for f in tmp:
            if not video_format_exists(f):
                basename = os.path.basename(filename)
                print(f"WARNING: {basename}:{idx}: unknown format {f}")

        formats += tmp

        if line[-1] != "\\":
            break

    if formats is None:
        print(f"Define for {token} not found in {filename}")
        return None

    formats = list(filter(video_format_exists, formats))

    return [video_format_info_from_string(f) for f in formats]


def score_endian(fmt, endian):
    native_endianness = [GstVideo.VideoFormat.ARGB64, GstVideo.VideoFormat.AYUV64]
    if fmt.format in native_endianness:
        return -1

    score = 0
    if fmt.flags & GstVideo.VideoFormatFlags.LE:
        score = 1
    if endian == "LE":
        return -score
    else:
        return score


def score_pixel_stride(fmt):
    # Prefer power-of-two strides over odd ones, e.g. xRGB over RGB
    def scale_non_power_of_two(x):
        if (x & (x - 1) == 0) and x != 0:
            return x
        else:
            return x * 100

    return [scale_non_power_of_two(x) for x in fmt.pixel_stride]


def sort_video_formats(formats, endian):
    return sorted(formats, key=lambda x: (-x.n_components,
                                          [-d for d in x.depth],
                                          x.w_sub,
                                          x.h_sub,
                                          -x.n_planes,
                                          score_endian(x, endian),
                                          (x.flags & GstVideo.VideoFormatFlags.COMPLEX),
                                          not (x.flags & GstVideo.VideoFormatFlags.YUV),
                                          score_pixel_stride(x),
                                          [s for s in x.pixel_stride],
                                          [o for o in x.poffset],
                                          x.name))


def video_formats_to_c(token, formats, bracket):
    s = [f'#define {token} "']

    if bracket:
        s[-1] += "{ "

    for f in formats:
        if len(s[-1]) > 80 - len(f.name) - len(', " \\'):
            s[-1] += ', " \\'
            s.append('    "')
        if s[-1][-2:] not in (' "', '{ '):
            s[-1] += ", "
        s[-1] += f.name

    if bracket:
        s[-1] += " }"

    s[-1] += '"'
    return s


def generate_c_code(token, formats_be, formats_le, bracket):
    s = ["#if G_BYTE_ORDER == G_BIG_ENDIAN"]
    s += video_formats_to_c(token, formats_be, bracket)
    s += ["#elif G_BYTE_ORDER == G_LITTLE_ENDIAN"]
    s += video_formats_to_c(token, formats_le, bracket)
    s += ["#endif"]
    return s


def main(args):
    formats = parse_format_from_c(args.token, args.filename)
    if formats is None:
        return 1

    formats_be = sort_video_formats(formats, "BE")
    formats_le = sort_video_formats(formats, "LE")

    # We only check BE order as the list are now generated
    if formats_be == formats:
        print("Formats are properly ordered.")
        return 0

    basename = os.path.basename(args.filename)
    print(f"ERROR: Formats order differ for {args.token} in {args.filename}")
    print("This can be fixed by replacing the define with:\n")
    for c_line in generate_c_code(args.token, formats_be, formats_le,
            args.bracket):
        print(c_line)
    return 1


if __name__ == '__main__':
    Gst.init(None)
    parser = argparse.ArgumentParser(prog='sort_video_formats',
                                     description='Sort video formats list')
    parser.add_argument('token')
    parser.add_argument('filename')
    parser.add_argument('-b', '--bracket', action='store_true')
    args = parser.parse_args()
    sys.exit(main(args))
