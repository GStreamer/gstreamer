import typing

if typing.TYPE_CHECKING:
    # Import stubs for type checking this file.
    from gi.repository import GstVideo
else:
    from gi.module import get_introspection_module
    GstVideo = get_introspection_module('GstVideo')

__all__: list[str] = []


def __video_info_from_caps(*args):
    raise NotImplementedError('VideoInfo.from_caps was removed, use VideoInfo.new_from_caps instead')


GstVideo.VideoInfo.from_caps = __video_info_from_caps  # type: ignore[method-assign]
