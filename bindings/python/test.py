import unittest
import rtspserver


def pubdir(obj):
    return [d for d in dir(obj) if not d.startswith('_')]

#print 'Module listing:', pubdir(rtspserver)

from rtspserver import Server, SessionPool, Session, MediaMapping, MediaFactory

#print 'Server listing: ', pubdir(Server)
#print 'MediaMapping listing: ', pubdir(MediaMapping)
#print 'MediaFactory listing: ', pubdir(MediaFactory)
#print 'SessionPool listing: ', pubdir(SessionPool)
#print 'Session listing: ', pubdir(Session)


class ServerTestCase(unittest.TestCase):

    def setUp(self):
        self.server = Server()

    def tearDown(self):
        del self.server

    def test_address(self):
        """ Server address set/get """
        addr = '1.2.3.4'
        self.server.set_address(addr)
        self.assertEquals(addr, self.server.get_address())

    def test_service(self):
        """ Server service set/get """
        service = '12345'
        self.server.set_service(service)
        self.assertEquals(service, self.server.get_service())

    def test_backlog(self):
        """ Server backlog set/get """
        backlog = 1234
        self.server.set_backlog(backlog)
        self.assertEquals(backlog, self.server.get_backlog())

    def test_session_pool(self):
        """ Server session pool set/get """
        pool = SessionPool()
        self.server.set_session_pool(pool)
        self.assertEquals(pool, self.server.get_session_pool())

    def test_media_mapping(self):
        """ Server media mapping set/get """
        mmap = MediaMapping()
        self.server.set_media_mapping(mmap)
        self.assertEquals(mmap, self.server.get_media_mapping())


class MediaMappingTestCase(unittest.TestCase):

    def setUp(self):
        self.mmap = MediaMapping()

    def tearDown(self):
        del self.mmap

    def test_factory(self):
        """ MediaMapping factory add/remove """
        self.factory = MediaFactory()
        self.mmap.add_factory("/test", self.factory)
        self.mmap.remove_factory("/test")


class MediaFactoryTestCase(unittest.TestCase):

    def setUp(self):
        self.factory = MediaFactory()

    def tearDown(self):
        del self.factory

    def test_launch(self):
        """ MediaFactory launch set/get """
        launch = "videotestsrc ! xvimagesink"
        self.factory.set_launch(launch)
        self.assertEquals(launch, self.factory.get_launch())

    def test_shared(self):
        """ MediaFactory shared set/is """
        self.factory.set_shared(True)
        self.assert_(self.factory.is_shared())
        self.factory.set_shared(False)
        self.assert_(not self.factory.is_shared())

    def test_eos_shutdown(self):
        """ MediaFactory eos_shutdown set/is """
        self.factory.set_eos_shutdown(True)
        self.assert_(self.factory.is_eos_shutdown())
        self.factory.set_eos_shutdown(False)
        self.assert_(not self.factory.is_eos_shutdown())



def alltests():
    tests = []

    for p in dir(ServerTestCase):
        try:
            if 'test_' in p:
                tests.append(ServerTestCase(p))
        except:
            pass

    for p in dir(MediaMappingTestCase):
        try:
            if 'test_' in p:
                tests.append(MediaMappingTestCase(p))
        except:
            pass

    for p in dir(MediaFactoryTestCase):
        try:
            if 'test_' in p:
                tests.append(MediaFactoryTestCase(p))
        except:
            pass

    return unittest.TestSuite(tests)


unittest.TextTestRunner(verbosity=2).run(alltests())
