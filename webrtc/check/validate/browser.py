#!/usr/bin/env python3
#
# Copyright (c) 2018, Matthew Waters <matthew@centricular.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

from selenium import webdriver
from selenium.webdriver.support.wait import WebDriverWait
from selenium.webdriver.firefox.firefox_profile import FirefoxProfile
from selenium.webdriver.chrome.options import Options as COptions

def create_firefox_driver():
    capabilities = webdriver.DesiredCapabilities().FIREFOX.copy()
    capabilities['acceptSslCerts'] = True
    profile = FirefoxProfile()
    profile.set_preference ('media.navigator.streams.fake', True)
    profile.set_preference ('media.navigator.permission.disabled', True)

    return webdriver.Firefox(firefox_profile=profile, capabilities=capabilities)

def create_chrome_driver():
    capabilities = webdriver.DesiredCapabilities().CHROME.copy()
    capabilities['acceptSslCerts'] = True
    copts = COptions()
    copts.add_argument('--allow-file-access-from-files')
    copts.add_argument('--use-fake-ui-for-media-stream')
    copts.add_argument('--use-fake-device-for-media-stream')
    copts.add_argument('--enable-blink-features=RTCUnifiedPlanByDefault')

    return webdriver.Chrome(options=copts, desired_capabilities=capabilities)

def create_driver(name):
    if name == 'firefox':
        return create_firefox_driver()
    elif name == 'chrome':
        return create_chrome_driver()
    else:
        raise ValueError("Unknown browser name " + name)

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

class Browser(object):
    def __init__(self, driver, html_source):
        self.driver = driver
        self.html_source = html_source

    def get_peer_id(self):
        self.driver.get(self.html_source)
        peer_id = WebDriverWait(self.driver, 10).until(
            lambda x: x.find_element_by_id('peer-id'),
            message='Peer-id element was never seen'
        )
        WebDriverWait (self.driver, 10).until(
            lambda x: valid_int(peer_id.text),
            message='Peer-id never became a number'
        )
        return int(peer_id.text)        
