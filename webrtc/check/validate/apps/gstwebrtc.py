#*- vi:si:et:sw=4:sts=4:ts=4:syntax=python
#
# Copyright (c) 2018 Matthew Waters <matthew@centricular.com>
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

import inspect
import os
import sys
import shutil

import tempfile

from launcher.baseclasses import TestsManager, TestsGenerator, GstValidateTest, ScenarioManager
from launcher.utils import DEFAULT_TIMEOUT

DEFAULT_BROWSERS = ['firefox', 'chrome']
DEFAULT_SCENARIOS = [
                "offer_answer",
                "vp8_send_stream"
                ]

BROWSER_SCENARIO_BLACKLISTS = {
    'firefox' : [
        'offer_answer', # fails to accept an SDP without any media sections
    ],
    'chrome' : [
    ],
}

class MutableInt(object):
    def __init__(self, value):
        self.value = value

class GstWebRTCTest(GstValidateTest):
    __used_ports = set()
    __last_id = MutableInt(10)

    @classmethod
    def __get_open_port(cls):
        while True:
            # hackish trick from
            # http://stackoverflow.com/questions/2838244/get-open-tcp-port-in-python?answertab=votes#tab-top
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.bind(("", 0))
            port = s.getsockname()[1]
            if port not in cls.__used_ports:
                cls.__used_ports.add(port)
                s.close()
                return port

            s.close()

    @classmethod
    def __get_available_peer_id(cls):
        peerid = cls.__last_id.value
        cls.__last_id.value += 2
        return peerid

    def __init__(self, classname, tests_manager, scenario, browser, timeout=DEFAULT_TIMEOUT):
        super().__init__("python3",
                        classname,
                        tests_manager.options,
                        tests_manager.reporter,
                        timeout=timeout,
                        scenario=scenario)
        self.webrtc_server = None
        filename = inspect.getframeinfo (inspect.currentframe ()).filename
        self.current_file_path = os.path.dirname (os.path.abspath (filename))
        self.certdir = None
        self.browser = browser

    def launch_server(self):
        if self.options.redirect_logs == 'stdout':
            self.webrtcserver_logs = sys.stdout
        elif self.options.redirect_logs == 'stderr':
            self.webrtcserver_logs = sys.stderr
        else:
            self.webrtcserver_logs = open(self.logfile + '_webrtcserver.log', 'w+')
            self.extra_logfiles.add(self.webrtcserver_logs.name)

        generate_certs_location = os.path.join(self.current_file_path, "..", "..", "..", "signalling", "generate_cert.sh")
        self.certdir = tempfile.mkdtemp()
        command = [generate_certs_location, self.certdir]

        server_env = os.environ.copy()

        subprocess.run(command,
                         stderr=self.webrtcserver_logs,
                         stdout=self.webrtcserver_logs,
                         env=server_env)

        self.server_port = self.__get_open_port()

        server_location = os.path.join(self.current_file_path, "..", "..", "..", "signalling", "simple_server.py")
        command = [server_location, "--cert-path", self.certdir, "--addr", "127.0.0.1", "--port", str(self.server_port)]

        self.webrtc_server = subprocess.Popen(command,
                                              stderr=self.webrtcserver_logs,
                                              stdout=self.webrtcserver_logs,
                                              env=server_env)
        while True:
            s = socket.socket()
            try:
                s.connect((("127.0.0.1", self.server_port)))
                break
            except ConnectionRefusedError:
                time.sleep(0.1)
                continue
            finally:
                s.close()

        return ' '.join(command)

    def build_arguments(self):
        gst_id = self.__get_available_peer_id()
        web_id = gst_id + 1

        self.add_arguments(os.path.join(self.current_file_path, '..', 'webrtc_validate.py'))
        self.add_arguments('--server')
        self.add_arguments("wss://127.0.0.1:%s" % (self.server_port,))
        self.add_arguments('--browser')
        self.add_arguments(self.browser)
        self.add_arguments("--html-source")
        html_page = os.path.join(self.current_file_path, '..', 'web', 'single_stream.html')
        html_params = '?server=127.0.0.1&port=' + str(self.server_port) + '&id=' + str(web_id)
        self.add_arguments("file://" + html_page + html_params)
        self.add_arguments('--peer-id')
        self.add_arguments(str(web_id))
        self.add_arguments(str(gst_id))

    def close_logfile(self):
        super().close_logfile()
        if not self.options.redirect_logs:
            self.webrtcserver_logs.close()

    def process_update(self):
        res = super().process_update()
        if res:
            kill_subprocess(self, self.webrtc_server, DEFAULT_TIMEOUT)
            self.__used_ports.remove(self.server_port)
            if self.certdir:
                shutil.rmtree(self.certdir, ignore_errors=True)
                self.certdir

        return res

class GstWebRTCTestsManager(TestsManager):
    scenarios_manager = ScenarioManager()
    name = "webrtc"

    def __init__(self):
        super(GstWebRTCTestsManager, self).__init__()
        self.loading_testsuite = self.name

    def webrtc_server_address(self):
        return "wss://127.0.0.1:8443"

    def populate_testsuite(self):
        self.add_scenarios (DEFAULT_SCENARIOS)

        scenarios = [(scenario_name, self.scenarios_manager.get_scenario(scenario_name))
                     for scenario_name in self.get_scenarios()]

        for name, scenario in scenarios:
            if not scenario:
                self.warning("Could not find scenario %s" % name)
                continue
            for browser in DEFAULT_BROWSERS:
                if name in BROWSER_SCENARIO_BLACKLISTS[browser]:
                    self.warning('Skipping broken test', name, 'for browser', browser)
                    continue
                classname = browser + '_' + name
                self.add_test(GstWebRTCTest(classname, self, scenario, browser))
