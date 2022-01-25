from ..module import get_introspection_module

import gi
gi.require_version('Gst', '1.0')

from gi.repository import Gst  # noqa

GstVideo = get_introspection_module('GstVideo')
__all__ = []


def __video_info_from_caps(*args):
    raise NotImplementedError('VideoInfo.from_caps was removed, use VideoInfo.new_from_caps instead')


GstVideo.VideoInfo.from_caps = __video_info_from_caps
