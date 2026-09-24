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

    def __init__(self, encodings=("utf-16",), options=None, capabilities=None):
        # stderr goes to a file: a pipe that nobody reads would block the server once it is full.
        self.log = tempfile.TemporaryFile()
        self.process = subprocess.Popen([SERVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log)
        self.next_id = 1
        self.buffer = b""
        self.notifications = []
        self.versions = {}
        try:
            result = self.request("initialize", {
                "processId": os.getpid(), "rootUri": None,
                "capabilities": {"general": {"positionEncodings": list(encodings)}, **(capabilities or {})},
                "initializationOptions": options or {},
            })
        except BaseException:
            self.kill()
            raise
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
    # Cleanups instead of tearDown, which does not run when setUp fails.
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.client = None

    def write(self, name, text):
        path = os.path.join(os.path.realpath(self.directory.name), name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return path

    def start(self, **kwargs):
        self.client = Client(**kwargs)
        self.addCleanup(self.client.kill)
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

    def test_line_separator_in_string(self):
        # The compiler also ends lines at U+2028, clients do not. The error on the last line must stay there.
        text = "🏁 🍇\n  😀 🔤a\u2028b🔤❗️\n  😀 🔤a🔤 ➕ 1❗️\n🍉\n"
        path = self.write("main.emojic", text)
        client = self.start()
        client.open(path)
        diagnostic = client.diagnostics(path)[0]
        self.assertEqual(diagnostic["range"]["start"], {"line": 2, "character": position(text, "➕")["character"]})

    def test_include_in_comment_is_ignored(self):
        self.write("app/old.🍇", "💭 📜 🔤b.🍇🔤\n🏁 🍇🍉\n")
        b = self.write("app/b.🍇", "🏁 🍇\n  😀 🔤a🔤 ➕ 1❗️\n🍉\n")
        client = self.start()
        client.open(b)
        # b is checked on its own, so its error is found.
        self.assertEqual(len(client.diagnostics(b)), 1)

    def test_include_added_on_save(self):
        a = self.write("app/a.🍇", "🐇 🐠 🍇 🆕 🍇🍉 🍉\n🏁 🍇🍉\n")
        b = self.write("app/b.🍇", "🐇 🐟 🍇\n  ❗️ 🐽 🍇\n    🆕🐠❗️ ➡️ x\n  🍉\n🍉\n")
        client = self.start()
        client.open(b)
        self.assertGreater(len(client.diagnostics(b)), 0)  # 🐠 is unknown and there is no 🏁 on its own.
        client.open(a)
        client.diagnostics(a)
        included = "📜 🔤b.🍇🔤\n🐇 🐠 🍇 🆕 🍇🍉 🍉\n🏁 🍇🍉\n"
        client.change(a, included)
        with open(a, "w", encoding="utf-8") as f:
            f.write(included)
        client.notify("textDocument/didSave", {"textDocument": {"uri": uri(a)}})
        # b is now checked as part of a. Whatever is published for b last must be the result of that.
        last = None
        try:
            while True:
                last = client.diagnostics(b, timeout=2)
        except TimeoutError:
            pass
        self.assertEqual(last, [])

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

    def test_invalid_text_does_not_crash_the_server(self):
        path = self.write("main.emojic", HELLO)
        client = self.start()
        # A stray Latin-1 byte, which is not UTF-8, and a lone surrogate.
        for text in (b'"\xe9\xff ' + "😀".encode() + b'"', b'"\\udc00"'):
            body = (b'{"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": "' +
                    uri(path).encode() + b'", "languageId": "emojicode", "version": 1, "text": ' + text + b'}}}')
            client.process.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
            client.process.stdin.flush()
            self.assertGreaterEqual(len(client.diagnostics(path)), 1)
        self.assertEqual(client.shutdown(), 0)

    def test_invalid_json_is_a_parse_error(self):
        client = self.start()
        client.process.stdin.write(b"Content-Length: 5\r\n\r\n{\"a\":")
        client.process.stdin.flush()
        self.assertEqual(client.receive()["error"]["code"], -32700)
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


FISH = """📗 A fish that can swim. 📗
🐇 🐟 🍇
  🖍🆕 depth 🔢 ⬅️ 0

  📗 Makes the fish swim down by meters. 📗
  ❗️ 🏊 meters 🔢 ➡️ 🔢 🍇
    depth ⬅️➕ meters
    ↩️ depth
  🍉

  🆕 🍇🍉
🍉

🏁 🍇
  🆕🐟❗️ ➡️ fish
  🏊 fish 5❗️ ➡️ 🖍🆕 total
  🍿 1 2 3 🍆 ➡️ list
  🔂 item list 🍇
    total ⬅️➕ item
  🍉
  😀 🔤Depth 🧲total🧲🔤❗️
🍉
"""


def position(text, needle, occurrence=1, encoding="utf-16"):
    """Returns the LSP position of the n-th occurrence of needle in text. Only occurrences outside of comments
    count. If needle contains a |, the position is that of the |, which is not part of the text."""
    offset = needle.find("|")
    needle = needle.replace("|", "")
    index = -1
    for _ in range(occurrence):
        index = text.index(needle, index + 1)
        while "📗" in text[text.rfind("\n", 0, index) + 1:index]:
            index = text.index(needle, index + 1)
    index += max(offset, 0)
    line = text.count("\n", 0, index)
    column = text[text.rfind("\n", 0, index) + 1:index]
    return {"line": line, "character": utf16_length(column) if encoding == "utf-16" else len(column)}


class NavigationTests(ServerTestCase):
    def setUp(self):
        super().setUp()
        self.path = self.write("fish.emojic", FISH)
        self.client = self.start()
        self.client.open(self.path)
        self.assertEqual(self.client.diagnostics(self.path), [])

    def request(self, method, needle, occurrence=1):
        return self.client.request(method, {"textDocument": {"uri": uri(self.path)},
                                            "position": position(FISH, needle, occurrence)})

    def hover(self, needle, occurrence=1):
        result = self.request("textDocument/hover", needle, occurrence)
        return result["contents"]["value"] if result else None

    def definition(self, needle, occurrence=1):
        result = self.request("textDocument/definition", needle, occurrence)
        if result is None:
            return None
        return urllib.parse.unquote(result["uri"]), result["range"]["start"]

    def test_hover_method_call(self):
        text = self.hover("🏊", 2)
        self.assertIn("❗️ 🏊 meters 🔢 ➡️ 🔢", text)
        self.assertIn("Makes the fish swim down by meters.", text)

    def test_hover_type_with_documentation(self):
        self.assertIn("A fish that can swim.", self.hover("🐟", 2))

    def test_hover_variables(self):
        self.assertIn("fish 🐟", self.hover("fish", 2))
        self.assertIn("🖍 total 🔢", self.hover("total"))
        self.assertIn("list 🍨🐚🔢", self.hover("list", 2))
        self.assertIn("depth 🔢", self.hover("depth", 2))

    def test_hover_standard_library(self):
        self.assertIn("😀", self.hover("😀"))

    def test_no_hover_on_compiler_generated_code(self):
        self.assertIsNone(self.hover("🔂"))

    def test_definition_of_method_and_type(self):
        self.assertEqual(self.definition("🏊", 2), (urllib.parse.unquote(uri(self.path)), position(FISH, "🏊")))
        self.assertEqual(self.definition("🐟", 2)[1], position(FISH, "🐟"))

    def test_definition_of_variables_is_their_name(self):
        self.assertEqual(self.definition("meters", 2)[1], position(FISH, "meters"))
        self.assertEqual(self.definition("fish", 2)[1], position(FISH, "fish"))
        self.assertEqual(self.definition("item", 2)[1], position(FISH, "item"))
        self.assertEqual(self.definition("depth", 2)[1], position(FISH, "depth"))

    def test_definition_in_standard_library(self):
        path, start = self.definition("😀")
        self.assertTrue(path.endswith("🏛"), path)

    def test_semantic_tokens(self):
        legend = self.client.capabilities["semanticTokensProvider"]["legend"]
        data = self.client.request("textDocument/semanticTokens/full",
                                   {"textDocument": {"uri": uri(self.path)}})["data"]
        tokens = {}
        line = character = 0
        lines = FISH.split("\n")
        for i in range(0, len(data), 5):
            delta_line, delta_start, length, kind, modifiers = data[i:i + 5]
            line += delta_line
            character = character + delta_start if delta_line == 0 else delta_start
            text = lines[line].encode("utf-16-le")[character * 2:(character + length) * 2].decode("utf-16-le")
            names = [m for bit, m in enumerate(legend["tokenModifiers"]) if modifiers & (1 << bit)]
            tokens.setdefault(text, (legend["tokenTypes"][kind], names))
        self.assertEqual(tokens["🐟"], ("class", ["declaration"]))
        self.assertEqual(tokens["🏊"], ("method", ["declaration"]))
        self.assertEqual(tokens["depth"], ("property", ["declaration"]))
        self.assertEqual(tokens["😀"], ("method", ["defaultLibrary"]))
        self.assertEqual(tokens["🔢"], ("struct", ["defaultLibrary"]))
        self.assertEqual(tokens["fish"][0], "variable")
        self.assertEqual(tokens["🔤Depth 🧲"][0], "string")
        self.assertEqual(tokens["🏁"][0], "keyword")

    def test_semantic_tokens_wait_for_the_check_of_a_change(self):
        # The change adds a line at the top, so 🐟 moves down. The tokens must be those of the changed text.
        self.client.change(self.path, "💭 A comment.\n" + FISH)
        data = self.client.request("textDocument/semanticTokens/full",
                                   {"textDocument": {"uri": uri(self.path)}})["data"]
        legend = self.client.capabilities["semanticTokensProvider"]["legend"]
        line = 0
        classes = []
        for i in range(0, len(data), 5):
            line += data[i]
            if legend["tokenTypes"][data[i + 3]] == "class":
                classes.append(line)
        self.assertEqual(classes[0], 2)

    def test_document_symbols(self):
        symbols = self.client.request("textDocument/documentSymbol", {"textDocument": {"uri": uri(self.path)}})
        self.assertEqual([s["name"] for s in symbols], ["🐟"])
        self.assertEqual([c["name"] for c in symbols[0]["children"]], ["depth", "🏊", "🆕"])


class CompletionTests(ServerTestCase):
    def complete(self, word, snippets=False):
        """Types word on a new line at the end of the 🏁 block of FISH and returns the completion items."""
        if self.client is None:
            self.path = self.write("fish.emojic", FISH)
            self.start(capabilities={"textDocument": {"completion": {"completionItem": {
                "snippetSupport": snippets}}}})
            self.client.open(self.path)
            self.client.diagnostics(self.path)
        text = FISH[:FISH.rindex("🍉")] + "  " + word + "\n🍉\n"
        self.client.change(self.path, text)
        line = text.count("\n") - 2
        result = self.client.request("textDocument/completion", {"textDocument": {"uri": uri(self.path)},
                                                                 "position": {"line": line,
                                                                              "character": 2 + len(word)}})
        self.assertTrue(result["isIncomplete"])
        return result["items"]

    def test_emoji_by_name(self):
        items = self.complete("grap")
        self.assertEqual(items[0]["textEdit"]["newText"], "🍇")
        self.assertEqual(items[0]["textEdit"]["range"]["start"], {"line": 21, "character": 2})

    def test_variables_first_even_with_errors(self):
        # The typed word is an undefined variable, which stops the analysis of the function.
        items = self.complete("to")
        self.assertEqual(items[0]["label"], "total")
        self.assertEqual(items[0]["detail"], "🔢")

    def test_parameters_of_closure_with_errors(self):
        # The typed word is an undefined variable inside a closure, which stops the analysis of the closure.
        # A closure's body cannot start with a variable, which would be read as a parameter.
        path = self.write("closure.emojic", "🏁 🍇\n  🍇 count 🔢\n    😀 🔡 co❗️❗️\n  🍉 ➡️ f\n🍉\n")
        client = self.start()
        client.open(path)
        client.diagnostics(path)
        items = client.request("textDocument/completion", {"textDocument": {"uri": uri(path)},
                                                           "position": {"line": 2, "character": 12}})["items"]
        self.assertEqual(items[0]["label"], "count")

    def test_variables_before_better_matches(self):
        # "sum" starts many emoji names and documentation words, but only a later word of the variable's name.
        path = self.write("sum.emojic", "🏁 🍇\n  1 ➡️ total_sum\n  sum\n🍉\n")
        client = self.start()
        client.open(path)
        client.diagnostics(path)
        items = client.request("textDocument/completion", {"textDocument": {"uri": uri(path)},
                                                           "position": {"line": 2, "character": 5}})["items"]
        self.assertEqual(items[0]["label"], "total_sum")

    def test_method_by_documentation(self):
        items = self.complete("swim")
        self.assertEqual(items[0]["textEdit"]["newText"], "🏊")
        self.assertIn("❗️ 🏊 meters 🔢 ➡️ 🔢", items[0]["detail"])

    def test_standard_library_method_by_documentation(self):
        items = self.complete("append")
        self.assertIn("🐻", [item["textEdit"]["newText"] for item in items[:5]])

    def test_type_by_documentation(self):
        items = self.complete("integer")
        self.assertEqual(items[0]["textEdit"]["newText"], "🔢")

    def test_keyword_without_snippet_support(self):
        items = self.complete("class")
        self.assertEqual(items[0]["textEdit"]["newText"], "🐇")
        self.assertNotIn("insertTextFormat", items[0])

    def test_no_completion_in_unterminated_string(self):
        # While a string is typed, it runs to the end of the file, which the lexer rejects.
        path = self.write("typing.emojic", "🏁 🍇\n  😀 🔤grap\n🍉\n")
        client = self.start()
        client.open(path)
        client.diagnostics(path)
        result = client.request("textDocument/completion", {"textDocument": {"uri": uri(path)},
                                                            "position": {"line": 1, "character": 11}})
        self.assertEqual(result["items"], [])

    def test_no_completion_in_strings_and_comments(self):
        path = self.write("strings.emojic", "🏁 🍇\n  😀 🔤grap🔤❗️ 💭 grap\n🍉\n")
        client = self.start()
        client.open(path)
        client.diagnostics(path)
        for character in (9, 23):
            result = client.request("textDocument/completion", {"textDocument": {"uri": uri(path)},
                                                                "position": {"line": 1, "character": character}})
            self.assertEqual(result["items"], [], character)


    def test_keyword_snippet(self):
        items = self.complete("class", snippets=True)
        self.assertEqual(items[0]["insertTextFormat"], 2)
        self.assertTrue(items[0]["textEdit"]["newText"].startswith("🐇 ${1:🐟} 🍇"))


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1], verbosity=2)
