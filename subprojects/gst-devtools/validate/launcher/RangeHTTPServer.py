#!/usr/bin/env python3

# Portions Copyright (C) 2009,2010  Xyne
# Portions Copyright (C) 2011 Sean Goller
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# (version 2) as published by the Free Software Foundation.
#
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.


"""Simple HTTP request handler with GET, HEAD, PUT and DELETE commands.
    This serves files from the current directory and any of its
    subdirectories. The MIME type for files is determined by
    calling the .guess_type() method.

    The GET and HEAD requests are identical except that the HEAD
    request omits the actual contents of the file.

    Administrative API endpoints:
        PUT /admin/status-rules
            Request body: {
                "path": "/path/to/file",
                "status_code": 503,
                "repeat": 3,  # optional
                "during": 6.0  # optional (duration for which the rule is active, in seconds)
            }
            Description: Configure a path to return a specific status code. The rule can be
            set to expire after a number of requests (repeat) or after a time duration (during).

        DELETE /admin/status-rules/<encoded-path>
            Description: Remove a previously configured status rule for the specified path.

        PUT /admin/failure-counts/start
            Request body: {
                "path": "/path/to/file"
            }
            Description: Start counting the number of forced failures (via status-rules) for
            the specified path. The count starts at 0 when this endpoint is called.

        GET /admin/failure-counts/<encoded-path>
            Response body: {
                "path": "/path/to/file",
                "count": 42
            }
            Description: Get the current count of forced failures for the specified path.
            Returns 404 if the path is not being monitored.
"""


__version__ = "0.1"

__all__ = ["RangeHTTPRequestHandler"]

import os
import json
import sys

from socketserver import ThreadingMixIn

import posixpath
import http.server
import urllib.parse
import html
import shutil
import mimetypes
import io
import time


_bandwidth = 0


def debug(msg):
    print(f'msg: {msg}', file=sys.stderr)


class ThreadingSimpleServer(ThreadingMixIn, http.server.HTTPServer):
    def server_bind(self):
        super().server_bind()
        print(f"PORT: {self.server_port}")
        sys.stdout.flush()


class RangeHTTPRequestHandler(http.server.BaseHTTPRequestHandler):

    """Simple HTTP request handler with GET and HEAD commands.
    This serves files from the current directory and any of its
    subdirectories.  The MIME type for files is determined by
    calling the .guess_type() method.

    The GET and HEAD requests are identical except that the HEAD
    request omits the actual contents of the file.
    """
    # Class-level dictionaries to store forced return codes and their repeat counts
    forced_return_codes = {}
    forced_failure_counts = {}
    server_version = "RangeHTTP/" + __version__

    def start_counting_failure(self, data):
        if not isinstance(data, dict) or 'path' not in data:
            self.send_error(400, "Invalid request body format")
            return

        # Start counting failures for this path
        self.__class__.forced_failure_counts[data['path']] = 0

        self.send_response(201)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps({'status': 'started'}).encode('utf-8'))

    def do_PUT(self):
        """Handle PUT requests for setting status rules."""
        if self.path not in ["/admin/failure-counts/start", "/admin/status-rules"]:
            self.send_error(404, "Not Found")
            return

        content_length = int(self.headers.get('Content-Length', 0))
        if content_length == 0:
            self.send_error(400, "Missing request body")
            return

        try:
            body = self.rfile.read(content_length).decode('utf-8')
            data = json.loads(body)

            if self.path == "/admin/failure-counts/start":
                self.start_counting_failure(data)
                return

            if not isinstance(data, dict):
                self.send_error(400, "Invalid request body format - expected JSON object")
                return
            if 'path' not in data:
                self.send_error(400, "Invalid request body format - missing required 'path' field")
                return
            if 'status_code' not in data:
                self.send_error(400, "Invalid request body format - missing required 'status_code' field")
                return

            forcereturn = {
                'code': int(data['status_code'])
            }

            if 'repeat' in data:
                repeat_count = int(data['repeat'])
                if repeat_count > 0:
                    forcereturn['repeat'] = repeat_count
                    debug(f"Will remove status rule after {repeat_count} GET requests")

            if 'during' in data:
                forcereturn['until'] = time.time() + float(data['during'])
                debug(f"Will remove status rule after {data['during']} seconds")

            self.__class__.forced_return_codes[data['path']] = forcereturn

            self.send_response(201)  # Created
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({'status': 'created'}).encode('utf-8'))

        except (ValueError, json.JSONDecodeError) as e:
            self.send_error(400, f"Invalid request: {str(e)}")
            return

    def do_DELETE(self):
        """Handle DELETE requests for removing status rules."""
        if not self.path.startswith("/admin/status-rules/"):
            self.send_error(404, "Not Found")
            return

        # Extract the path from the URL (need to decode it as it might contain slashes)
        encoded_path = self.path[len("/admin/status-rules/"):]
        try:
            path = urllib.parse.unquote(encoded_path)
        except Exception:
            self.send_error(400, "Invalid path encoding")
            return

        if path in self.__class__.forced_return_codes:
            del self.__class__.forced_return_codes[path]
            self.send_response(204)  # No Content
            self.end_headers()
        else:
            self.send_error(404, "Status rule not found")

    def get_failure_count(self):
        failure_count_path = "/admin/failure-counts"
        debug(f"Path is: {self.path} == {failure_count_path} ==>  {self.path.startswith(failure_count_path)}")
        if not self.path.startswith(failure_count_path):
            return False

        # Extract the path from the URL
        encoded_path = self.path[len(failure_count_path):]
        try:
            path = urllib.parse.unquote(encoded_path)
        except Exception:
            self.send_error(400, "Invalid path encoding")
            return True

        if path in self.__class__.forced_failure_counts:
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            response = {
                'path': path,
                'count': self.__class__.forced_failure_counts[path]
            }
            self.wfile.write(json.dumps(response).encode('utf-8'))
        else:
            self.send_error(404, f"Path {path} is not being monitored {self.__class__.forced_failure_counts}")

        return True

    def do_GET(self):
        """Serve a GET request."""
        if self.get_failure_count():
            return

        # Check if there's a forced return code for this path
        parsed_url = urllib.parse.urlparse(self.path)
        forcereturn = self.__class__.forced_return_codes.get(parsed_url.path)
        debug(f"Rules {self.__class__.forced_return_codes}")
        if forcereturn:
            repeat = forcereturn.get('repeat')
            if repeat:
                repeat -= 1
                if repeat <= 0:
                    del self.__class__.forced_return_codes[parsed_url.path]
                    debug(f"Stopped forcing return code for {parsed_url.path}")
                else:
                    forcereturn['repeat'] = repeat

                    self.__class__.forced_return_codes[parsed_url.path] = forcereturn

            force_until = forcereturn.get('until')
            if force_until:
                remaining_forcing_time = force_until - time.time()
                debug("Forcing for {}s".format(remaining_forcing_time))
                if remaining_forcing_time <= 0:
                    del self.__class__.forced_return_codes[parsed_url.path]
                    debug(f"Stopped forcing return code for {parsed_url.path}")
                    forcereturn = None

            if forcereturn is not None:
                if parsed_url.path in self.__class__.forced_failure_counts:
                    self.__class__.forced_failure_counts[parsed_url.path] += 1
                self.send_response(forcereturn['code'])
                self.end_headers()
                return
        debug("Got request for {}".format(self.path))

        f, start_range, end_range = self.send_head()
        debug("Got values of {} and {}".format(start_range, end_range))
        if f:
            f.seek(start_range, 0)
            chunk = 0x1000
            total = 0
            while chunk > 0:
                if start_range + chunk > end_range:
                    chunk = end_range - start_range

                if _bandwidth != 0:
                    time_to_sleep = float(float(chunk) / float(_bandwidth))
                    time.sleep(time_to_sleep)

                try:
                    self.wfile.write(f.read(chunk))
                except Exception:
                    break
                total += chunk
                start_range += chunk
            f.close()

    def do_HEAD(self):
        """Serve a HEAD request."""
        f, start_range, end_range = self.send_head()
        if f:
            f.close()

    def send_head(self):
        """Common code for GET and HEAD commands.

        This sends the response code and MIME headers.

        Return value is either a file object (which has to be copied
        to the outputfile by the caller unless the command was HEAD,
        and must be closed by the caller under all circumstances), or
        None, in which case the caller has nothing further to do.

        """
        path = self.translate_path(self.path)
        f = None
        if os.path.isdir(path):
            if not self.path.endswith("/"):
                # redirect browser
                self.send_response(301)
                self.send_header("Location", self.path + "/")
                self.end_headers()
                return (None, 0, 0)
            for index in "index.html", "index.html":
                index = os.path.join(path, index)
                if os.path.exists(index):
                    path = index
                    break
            else:
                return self.list_directory(path)
        ctype = self.guess_type(path)

        try:
            # Always read in binary mode. Opening files in text mode may cause
            # newline translations, making the actual size of the content
            # transmitted *less* than the content-length!
            f = open(path, "rb")
        except IOError:
            self.send_error(404, "File not found")
            return (None, 0, 0)

        if "Range" in self.headers:
            self.send_response(206)  # partial content response
        else:
            self.send_response(200)

        self.send_header("Content-type", ctype)
        file_size = os.path.getsize(path)

        start_range = 0
        end_range = file_size

        self.send_header("Accept-Ranges", "bytes")
        if "Range" in self.headers:
            s, e = self.headers['range'][6:].split('-', 1)  # bytes:%d-%d
            sl = len(s)
            el = len(e)

            if sl:
                start_range = int(s)
                if el:
                    end_range = int(e) + 1
            elif el:
                start_range = file_size - min(file_size, int(e))

        self.send_header("Content-Range", "bytes {}-{}/{}".format(start_range, end_range, file_size))
        self.send_header("Content-Length", end_range - start_range)
        self.end_headers()

        debug("Sending bytes {} to {}...".format(start_range, end_range))
        return (f, start_range, end_range)

    def list_directory(self, path):
        """Helper to produce a directory listing (absent index.html).

                Return value is either a file object, or None (indicating an
                error).  In either case, the headers are sent, making the
                interface the same as for send_head().

                """
        try:
            lst = os.listdir(path)
        except OSError:
            self.send_error(404, "Access Forbidden")
            return None

        lst.sort(key=lambda file_name: file_name.lower())
        html_text = []

        displaypath = html.escape(urllib.parse.unquote(self.path))
        html_text.append('<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 3.2 Final//EN">')
        html_text.append("<html>\n<title>Directory listing for {}</title>\n".format(displaypath))
        html_text.append("<body>\n<h2>Directory listing for {}</h2>\n".format(displaypath))
        html_text.append("<hr>\n<ul>\n")

        for name in lst:
            fullname = os.path.join(path, name)
            displayname = linkname = name

            if os.path.isdir(fullname):
                displayname = name + "/"
                linkname = name + "/"

            if os.path.islink(fullname):
                displayname = name + "@"

            html_text.append('<li><a href = "{}">{}</a>\n'.format(urllib.parse.quote(linkname), html.escape(displayname)))

        html_text.append('</ul>\n</hr>\n</body>\n</html>\n')

        byte_encoded_string = "\n".join(html_text).encode("utf-8", "surrogateescape")
        f = io.BytesIO()
        f.write(byte_encoded_string)
        length = len(byte_encoded_string)

        f.seek(0)

        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.send_header("Content-length", str(length))
        self.end_headers()

        return (f, 0, length)

    def translate_path(self, path):
        """Translate a /-separated PATH to the local filename syntax.

        Components that mean special things to the local file system
        (e.g. drive or directory names) are ignored.  (XXX They should
        probably be diagnosed.)

        """
        # abandon query parameters
        path = path.split("?", 1)[0]
        path = path.split("#", 1)[0]
        path = posixpath.normpath(urllib.parse.unquote(path))
        words = path.split("/")
        words = filter(None, words)
        path = os.getcwd()

        for word in words:
            drive, word = os.path.splitdrive(word)
            head, word = os.path.split(word)
            if word in (os.curdir, os.pardir):
                continue
            path = os.path.join(path, word)
        return path

    def guess_type(self, path):
        """Guess the type of a file.

        Argument is a PATH (a filename).

        Return value is a string of the form type/subtype,
        usable for a MIME Content-type header.

        The default implementation looks the file's extension
        up in the table self.extensions_map, using application/octet-stream
        as a default; however it would be permissible (if
        slow) to look inside the data to make a better guess.

        """

        base, ext = posixpath.splitext(path)
        if ext in self.extension_map:
            return self.extension_map[ext]
        ext = ext.lower()
        if ext in self.extension_map:
            return self.extension_map[ext]
        else:
            return self.extension_map['']

    if not mimetypes.inited:
        mimetypes.init()
    extension_map = mimetypes.types_map.copy()
    extension_map.update({
        '': 'application/octet-stream',  # Default
            '.py': 'text/plain',
            '.c': 'text/plain',
            '.h': 'text/plain',
            '.mp4': 'video/mp4',
            '.ogg': 'video/ogg',
            '.java': 'text/plain',
    })


def test(handler_class=RangeHTTPRequestHandler, server_class=http.server.HTTPServer):
    http.server.test(handler_class, server_class)


if __name__ == "__main__":
    httpd = ThreadingSimpleServer(("0.0.0.0", int(sys.argv[1])), RangeHTTPRequestHandler)
    httpd.serve_forever()
    print("EXIT")
