import typing

if typing.TYPE_CHECKING:
    # Import stubs for type checking this file.
    from gi.repository import GstAudio
else:
    from gi.module import get_introspection_module
    GstAudio = get_introspection_module('GstAudio')


__all__: list[str] = []


def __audio_info_from_caps(*args):
    raise NotImplementedError('AudioInfo.from_caps was removed, use AudioInfo.new_from_caps instead')


GstAudio.AudioInfo.from_caps = __audio_info_from_caps  # type: ignore[method-assign]
