from ..module import get_introspection_module

import gi
gi.require_version('Gst', '1.0')

from gi.repository import Gst  # noqa

GstAudio = get_introspection_module('GstAudio')
__all__ = []


def __audio_info_from_caps(*args):
    raise NotImplementedError('AudioInfo.from_caps was removed, use AudioInfo.new_from_caps instead')


GstAudio.AudioInfo.from_caps = __audio_info_from_caps
