
from GstDebugViewer.Plugins import FeatureBase, PluginBase

class ColorizeLevels (FeatureBase):

    def attach (self, window):

        pass

    def detach (self, window):

        pass

class LevelColorSentinel (object):

    def processor (self, proc):

        for row in proc:

            yield None

class ColorizeCategories (FeatureBase):

    def attach (self, window):

        pass

    def detach (self, window):

        pass

class CategoryColorSentinel (object):

    def processor (self):

        pass

class Plugin (PluginBase):

    features = [ColorizeLevels, ColorizeCategories]


