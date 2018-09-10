#!/usr/bin/env python3

import os
import unittest
from selenium import webdriver
from selenium.webdriver.support.wait import WebDriverWait
from selenium.webdriver.firefox.firefox_profile import FirefoxProfile
from selenium.webdriver.chrome.options import Options as COptions
import webrtc_sendrecv as webrtc
import simple_server as sserver
import asyncio
import threading
import signal

import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst

thread = None
stop = None
server = None

class AsyncIOThread(threading.Thread):
    def __init__ (self, loop):
        threading.Thread.__init__(self)
        self.loop = loop

    def run(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()
        self.loop.close()
        print ("closed loop")

    def stop_thread(self):
        self.loop.call_soon_threadsafe(self.loop.stop)

async def run_until(server, stop_token):
    async with server:
        await stop_token
    print ("run_until done")

def setUpModule():
    global thread, server
    Gst.init(None)
    cacerts_path = os.environ.get('TEST_CA_CERT_PATH')
    loop = asyncio.new_event_loop()

    thread = AsyncIOThread(loop)
    thread.start()
    server = sserver.WebRTCSimpleServer('127.0.0.1', 8443, 20, False, cacerts_path)
    def f():
        global stop
        stop = asyncio.ensure_future(server.run())
    loop.call_soon_threadsafe(f)

def tearDownModule():
    global thread, stop
    stop.cancel()
    thread.stop_thread()
    thread.join()
    print("thread joined")

def valid_int(n):
    if isinstance(n, int):
        return True
    if isinstance(n, str):
        try:
            num = int(n)
            return True
        except:
            return False
    return False

def create_firefox_driver():
    capabilities = webdriver.DesiredCapabilities().FIREFOX.copy()
    capabilities['acceptSslCerts'] = True
    capabilities['acceptInsecureCerts'] = True
    profile = FirefoxProfile()
    profile.set_preference ('media.navigator.streams.fake', True)
    profile.set_preference ('media.navigator.permission.disabled', True)

    return webdriver.Firefox(firefox_profile=profile, capabilities=capabilities)

def create_chrome_driver():
    capabilities = webdriver.DesiredCapabilities().CHROME.copy()
    capabilities['acceptSslCerts'] = True
    capabilities['acceptInsecureCerts'] = True
    copts = COptions()
    copts.add_argument('--allow-file-access-from-files')
    copts.add_argument('--use-fake-ui-for-media-stream')
    copts.add_argument('--use-fake-device-for-media-stream')
    copts.add_argument('--enable-blink-features=RTCUnifiedPlanByDefault')

    return webdriver.Chrome(options=copts, desired_capabilities=capabilities)

class ServerConnectionTestCase(unittest.TestCase):
    def setUp(self):
        self.browser = create_firefox_driver()
#        self.browser = create_chrome_driver()
        self.addCleanup(self.browser.quit)
        self.html_source = os.environ.get('TEST_HTML_SOURCE')
        self.assertIsNot(self.html_source, None)
        self.assertNotEqual(self.html_source, '')
        self.html_source = 'file://' + self.html_source + '/index.html'

    def get_peer_id(self):
        self.browser.get(self.html_source)
        peer_id = WebDriverWait(self.browser, 5).until(
            lambda x: x.find_element_by_id('peer-id'),
            message='Peer-id element was never seen'
        )
        WebDriverWait (self.browser, 5).until(
            lambda x: valid_int(peer_id.text),
            message='Peer-id never became a number'
        )
        return int(peer_id.text)

    def testPeerID(self):
        self.get_peer_id()

    def testPerformCall(self):
        loop = asyncio.new_event_loop()
        thread = AsyncIOThread(loop)
        thread.start()
        peer_id = self.get_peer_id()
        client = webrtc.WebRTCClient(peer_id + 1, peer_id, 'wss://127.0.0.1:8443')

        async def do_things():
            await client.connect()
            async def stop_after(client, delay):
                await asyncio.sleep(delay)
                await client.stop()
            future = asyncio.ensure_future (stop_after (client, 5))
            res = await client.loop()
            thread.stop_thread()
            return res

        res = asyncio.run_coroutine_threadsafe(do_things(), loop).result()
        thread.join()
        print ("client thread joined")
        self.assertEqual(res, 0)

if __name__ == '__main__':
    unittest.main(verbosity=2)
