import sys
from common import gst, unittest

class RegistryPoolTest(unittest.TestCase):
    def testPluginList(self):
        plugins = gst.registry_pool_plugin_list()
        elements = map(lambda p: p.get_name(), plugins)
        assert 'gstcoreelements' in elements
        
    def testFeatureList(self):
        plugins = gst.registry_pool_feature_list(gst.ElementFactory)
        elements = map(lambda p: p.get_name(), plugins)
        assert 'fakesink' in elements, elements

if __name__ == "__main__":
    unittest.main()
