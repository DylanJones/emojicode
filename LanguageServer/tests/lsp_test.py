#!/usr/bin/env python3
"""Tests emojicode-lsp by running scripted Language Server Protocol sessions.

Usage: lsp_test.py [path to emojicode-lsp]

The default is build/LanguageServer/emojicode-lsp, which finds the standard library in the build directory.
"""
import json
import os
import select
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SERVER = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "LanguageServer",
                                                                             "emojicode-lsp"))


def uri(path):
    return "file://" + urllib.parse.quote(os.path.realpath(path))


def utf16_length(text):
    return len(text.encode("utf-16-le")) // 2


class Client:
    """A minimal LSP client that talks to a server process over stdio."""

    def __init__(self, encodings=("utf-16",), options=None):
        # stderr goes to a file: a pipe that nobody reads would block the server once it is full.
        self.log = tempfile.TemporaryFile()
        self.process = subprocess.Popen([SERVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log)
        self.next_id = 1
        self.buffer = b""
        self.notifications = []
        self.versions = {}
        result = self.request("initialize", {
            "processId": os.getpid(), "rootUri": None,
            "capabilities": {"general": {"positionEncodings": list(encodings)}},
            "initializationOptions": options or {},
        })
        self.capabilities = result["capabilities"]
        self.notify("initialized", {})

    def send(self, message):
        body = json.dumps(message, ensure_ascii=False).encode("utf-8")
        self.process.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.process.stdin.flush()

    def receive(self, timeout=20):
        deadline = time.time() + timeout
        while True:
            header_end = self.buffer.find(b"\r\n\r\n")
            if header_end >= 0:
                headers = self.buffer[:header_end].decode("ascii").split("\r\n")
                length = next(int(h.split(":")[1]) for h in headers if h.lower().startswith("content-length"))
                start = header_end + 4
                if len(self.buffer) >= start + length:
                    message = json.loads(self.buffer[start:start + length].decode("utf-8"))
                    self.buffer = self.buffer[start + length:]
                    return message
            remaining = deadline - time.time()
            if remaining <= 0 or not select.select([self.process.stdout], [], [], remaining)[0]:
                raise TimeoutError("No message from the server")
            chunk = os.read(self.process.stdout.fileno(), 65536)
            if not chunk:
                self.log.seek(0)
                raise EOFError("The server exited: " + self.log.read().decode("utf-8", "replace"))
            self.buffer += chunk

    def request(self, method, params):
        request_id = self.next_id
        self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        while True:
            message = self.receive()
            if message.get("id") == request_id and "method" not in message:
                if "error" in message:
                    raise RuntimeError(message["error"])
                return message["result"]
            self.notifications.append(message)

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def diagnostics(self, path, timeout=20):
        """Waits for the next diagnostics published for the file at path and returns them."""
        wanted = uri(path)
        deadline = time.time() + timeout
        while True:
            for i, message in enumerate(self.notifications):
                if message.get("method") == "textDocument/publishDiagnostics" and \
                        urllib.parse.unquote(message["params"]["uri"]) == urllib.parse.unquote(wanted):
                    del self.notifications[i]
                    return message["params"]["diagnostics"]
            self.notifications.append(self.receive(max(0.01, deadline - time.time())))

    def open(self, path, text=None):
        if text is None:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        self.versions[path] = 1
        self.notify("textDocument/didOpen", {"textDocument": {"uri": uri(path), "languageId": "emojicode",
                                                              "version": 1, "text": text}})

    def change(self, path, text):
        self.versions[path] += 1
        self.notify("textDocument/didChange", {"textDocument": {"uri": uri(path), "version": self.versions[path]},
                                               "contentChanges": [{"text": text}]})

    def close(self, path):
        self.notify("textDocument/didClose", {"textDocument": {"uri": uri(path)}})

    def shutdown(self):
        self.request("shutdown", None)
        self.notify("exit", None)
        return self.process.wait(timeout=10)

    def kill(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        self.process.stdin.close()
        self.process.stdout.close()
        self.log.close()


class ServerTestCase(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.client = None

    def tearDown(self):
        if self.client:
            self.client.kill()
        self.directory.cleanup()

    def write(self, name, text):
        path = os.path.join(os.path.realpath(self.directory.name), name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return path

    def start(self, **kwargs):
        self.client = Client(**kwargs)
        return self.client


HELLO = "🏁 🍇\n  😀 🔤Hello world!🔤❗️\n🍉\n"
# 🔡 has no ➕, so this is a type error. It is reported at ➕, after several emoji on the line.
TYPE_ERROR = "🏁 🍇\n  😀 🔤a🔤 ➕ 1❗️\n🍉\n"


class DiagnosticsTests(ServerTestCase):
    def test_initialize_and_shutdown(self):
        client = self.start()
        self.assertEqual(client.capabilities["positionEncoding"], "utf-16")
        self.assertEqual(client.capabilities["textDocumentSync"]["change"], 1)
        self.assertEqual(client.shutdown(), 0)

    def test_valid_program_has_no_diagnostics(self):
        path = self.write("hello.emojic", HELLO)
        client = self.start()
        client.open(path)
        self.assertEqual(client.diagnostics(path), [])

    def test_type_error_range_in_utf16(self):
        path = self.write("main.emojic", HELLO)
        client = self.start()
        # The file on disk is valid; the error is only in the editor's unsaved text.
        client.open(path, TYPE_ERROR)
        diagnostics = client.diagnostics(path)
        self.assertEqual(len(diagnostics), 1, diagnostics)
        diagnostic = diagnostics[0]
        self.assertEqual(diagnostic["severity"], 1)
        line = TYPE_ERROR.split("\n")[1]
        start = utf16_length(line[:line.index("➕")])
        self.assertEqual(diagnostic["range"]["start"], {"line": 1, "character": start}, diagnostic)
        self.assertEqual(diagnostic["range"]["end"]["line"], 1)
        self.assertGreater(diagnostic["range"]["end"]["character"], start)

        client.change(path, HELLO)
        self.assertEqual(client.diagnostics(path), [])

    def test_utf32_positions(self):
        path = self.write("main.emojic", TYPE_ERROR)
        client = self.start(encodings=("utf-8", "utf-32", "utf-16"))
        self.assertEqual(client.capabilities["positionEncoding"], "utf-32")
        client.open(path)
        diagnostic = client.diagnostics(path)[0]
        line = TYPE_ERROR.split("\n")[1]
        self.assertEqual(diagnostic["range"]["start"], {"line": 1, "character": line.index("➕")})

    def test_syntax_error(self):
        path = self.write("main.emojic", "🏁 🍇\n  😀 🔤unterminated\n🍉\n")
        client = self.start()
        client.open(path)
        diagnostics = client.diagnostics(path)
        self.assertGreaterEqual(len(diagnostics), 1)
        self.assertEqual(diagnostics[0]["severity"], 1)

    def test_error_in_included_file(self):
        main = self.write("app/main.🍇", "📜 🔤helper.🍇🔤\n🏁 🍇\n  🐽❗️\n🍉\n")
        helper = self.write("app/helper.🍇", "🐇 🐟 🍇🍉\n")
        client = self.start()
        # A method call to an undefined function in the helper file, which is only valid as part of main.🍇.
        client.open(helper, "🐇 🐟 🍇\n  🐇❗️ 🐽 🍇\n    🥶❗️\n  🍉\n🍉\n")
        diagnostics = client.diagnostics(helper)
        self.assertTrue(any(d["range"]["start"]["line"] == 2 for d in diagnostics), diagnostics)
        # main.🍇 is not open, but its own error is still reported.
        self.assertTrue(len(client.diagnostics(main)) >= 1)

    def test_debounce_checks_only_last_change(self):
        path = self.write("main.emojic", HELLO)
        client = self.start()
        client.open(path)
        self.assertEqual(client.diagnostics(path), [])
        for _ in range(5):
            client.change(path, TYPE_ERROR)
        client.change(path, HELLO)
        self.assertEqual(client.diagnostics(path), [])
        # Only one check ran for the burst of changes, so there are no more diagnostics.
        with self.assertRaises(TimeoutError):
            client.diagnostics(path, timeout=1)

    def test_close_clears_diagnostics(self):
        path = self.write("main.emojic", TYPE_ERROR)
        client = self.start()
        client.open(path)
        self.assertEqual(len(client.diagnostics(path)), 1)
        client.close(path)
        self.assertEqual(client.diagnostics(path), [])

    def test_unknown_request(self):
        client = self.start()
        with self.assertRaises(RuntimeError):
            client.request("textDocument/unknownThing", {})
        self.assertEqual(client.shutdown(), 0)

    def test_garbage_does_not_crash_the_server(self):
        path = self.write("main.emojic", HELLO)
        client = self.start()
        client.open(path)
        client.diagnostics(path)
        for text in ["🏁", "🍇🍇🍇", "🔤", "🐇 🐟 🍇 ❗️", "💭🔜", "🏁 🍇 ↩️", "📦 nonexistent 🏠", "🎍", "0x"]:
            client.change(path, text)
            client.diagnostics(path)
        self.assertEqual(client.shutdown(), 0)


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1], verbosity=2)
