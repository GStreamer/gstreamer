# Copyright (c) 2020, Matthew Waters <matthew@centricular.com>
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
import itertools

import tempfile

from launcher.baseclasses import TestsManager, GstValidateTest, ScenarioManager
from launcher.utils import DEFAULT_TIMEOUT

DEFAULT_BROWSERS = ['firefox', 'chrome']

# list of scenarios. These are the names of the actual scenario files stored
# on disk.
DEFAULT_SCENARIOS = [
        "offer_answer",
        "vp8_send_stream",
        "open_data_channel",
        "send_data_channel_string",
    ]

# various configuration changes that are included from other scenarios.
# key is the name of the override used in the name of the test
# value is the subdirectory where the override is placed
# changes some things about the test like:
# - who initiates the negotiation
# - bundle settings
SCENARIO_OVERRIDES = {
    # name : directory

    # who starts the negotiation
    'local' : 'local_initiates_negotiation',
    'remote' : 'remote_initiates_negotiation',

    # bundle-policy configuration
    # XXX: webrtcbin's bundle-policy=none is not part of the spec
    'none_compat' : 'bundle_local_none_remote_max_compat',
    'none_balanced' : 'bundle_local_none_remote_balanced',
    'none_bundle' : 'bundle_local_none_remote_max_bundle',
    'compat_compat' : 'bundle_local_max_compat_remote_max_compat',
    'compat_balanced' : 'bundle_local_max_compat_remote_balanced',
    'compat_bundle' : 'bundle_local_max_compat_remote_max_bundle',
    'balanced_compat' : 'bundle_local_balanced_remote_max_compat',
    'balanced_balanced' : 'bundle_local_balanced_remote_balanced',
    'balanced_bundle' : 'bundle_local_balanced_remote_bundle',
    'bundle_compat' : 'bundle_local_max_bundle_remote_max_compat',
    'bundle_balanced' : 'bundle_local_max_bundle_remote_balanced',
    'bundle_bundle' : 'bundle_local_max_bundle_remote_max_bundle',
}

bundle_options = ['compat', 'balanced', 'bundle']

# Given an override, these are the choices to choose from.  Each choice is a
# separate test
OVERRIDE_CHOICES = {
    'initiator' : ['local', 'remote'],
    'bundle' : ['_'.join(opt) for opt in itertools.product(['none'] + bundle_options, bundle_options)],
}

# Which scenarios support which override.  All the overrides will be chosen
SCENARIO_OVERRIDES_SUPPORTED = {
    "offer_answer" : ['initiator', 'bundle'],
    "vp8_send_stream" : ['initiator', 'bundle'],
    "open_data_channel" : ['initiator', 'bundle'],
    "send_data_channel_string" : ['initiator', 'bundle'],
}

# Things that don't work for some reason or another.
DEFAULT_BLACKLIST = [
    (r"webrtc\.firefox\.local\..*offer_answer",
     "Firefox doesn't like a SDP without any media"),
    (r"webrtc.*remote.*vp8_send_stream",
     "We can't match payload types with a remote offer and a sending stream"),
    (r"webrtc.*\.balanced_.*",
     "webrtcbin doesn't implement bundle-policy=balanced"),
    (r"webrtc.*\.none_bundle.*",
     "Browsers want a BUNDLE group if in max-bundle mode"),
]

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
        # each connection uses two peer ids
        peerid = cls.__last_id.value
        cls.__last_id.value += 2
        return peerid

    def __init__(self, classname, tests_manager, scenario, browser, scenario_override_includes=None, timeout=DEFAULT_TIMEOUT):
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
        self.scenario_override_includes = scenario_override_includes

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
        self.add_arguments("--name")
        self.add_arguments(self.classname)
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

        return res

    def get_subproc_env(self):
        env = super().get_subproc_env()
        if not self.scenario_override_includes:
            return env

        # this feels gross...
        paths = env.get('GST_VALIDATE_SCENARIOS_PATH', '').split(os.pathsep)
        new_paths = []
        for p in paths:
            new_paths.append(p)
            for override_path in self.scenario_override_includes:
                new_p = os.path.join(p, override_path)
                if os.path.exists (new_p):
                    new_paths.append(new_p)
        env['GST_VALIDATE_SCENARIOS_PATH'] = os.pathsep.join(new_paths)

        return env

class GstWebRTCTestsManager(TestsManager):
    scenarios_manager = ScenarioManager()
    name = "webrtc"

    def __init__(self):
        super(GstWebRTCTestsManager, self).__init__()
        self.loading_testsuite = self.name
        self._scenarios = []

    def add_scenarios(self, scenarios):
        if isinstance(scenarios, list):
            self._scenarios.extend(scenarios)
        else:
            self._scenarios.append(scenarios)

        self._scenarios = list(set(self._scenarios))

    def set_scenarios(self, scenarios):
        self._scenarios = []
        self.add_scenarios(scenarios)

    def get_scenarios(self):
        return self._scenarios

    def populate_testsuite(self):
        self.add_scenarios (DEFAULT_SCENARIOS)
        self.set_default_blacklist(DEFAULT_BLACKLIST)

    def list_tests(self):
        if self.tests:
            return self.tests

        scenarios = [(scenario_name, self.scenarios_manager.get_scenario(scenario_name))
                     for scenario_name in self.get_scenarios()]

        for browser in DEFAULT_BROWSERS:
            for name, scenario in scenarios:
                if not scenario:
                    self.warning("Could not find scenario %s" % name)
                    continue
                if not SCENARIO_OVERRIDES_SUPPORTED[name]:
                    # no override choices supported
                    classname = browser + '.' + name
                    print ("adding", classname)
                    self.add_test(GstWebRTCTest(classname, self, scenario, browser))
                else:
                    for overrides in itertools.product(*[OVERRIDE_CHOICES[c] for c in SCENARIO_OVERRIDES_SUPPORTED[name]]):
                        oname = '.'.join (overrides)
                        opaths = [SCENARIO_OVERRIDES[p] for p in overrides]
                        classname = browser + '.' + oname + '.' + name
                        print ("adding", classname)
                        self.add_test(GstWebRTCTest(classname, self, scenario, browser, opaths))

        return self.tests
