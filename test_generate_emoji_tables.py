"""Regression tests for Compiler/Lex/generate_emoji_tables.py, run offline against fixtures in a temporary copy."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.abspath(__file__))
BODY = """# emoji-data.txt
0023          ; Emoji                # E0.0   [1] (#️)       number sign
1F44D         ; Emoji                # E0.6   [1] (👍)       thumbs up
1F3FB..1F3FF  ; Emoji_Modifier       # E1.0   [5] (🏻..🏿)    light skin tone..dark skin tone
1F44D         ; Emoji_Modifier_Base  # E0.6   [1] (👍)       thumbs up
"""


class GenerateEmojiTablesTest(unittest.TestCase):
    def run_generator(self, header):
        with tempfile.TemporaryDirectory() as tmp:
            for rel in ("Compiler/Lex/generate_emoji_tables.py", "Compiler/Lex/EmojiTokenization.cpp",
                        "Compiler/Lex/EmojiTokenization.hpp", "docs/grammar.ebnf"):
                os.makedirs(os.path.dirname(os.path.join(tmp, rel)), exist_ok=True)
                shutil.copy(os.path.join(ROOT, rel), os.path.join(tmp, rel))
            data = os.path.join(tmp, "emoji-data.txt")
            with open(data, "w", encoding="utf-8") as f:
                f.write(header + BODY)
            result = subprocess.run([sys.executable, os.path.join(tmp, "Compiler/Lex/generate_emoji_tables.py"), data],
                                    capture_output=True, text=True)
            with open(os.path.join(tmp, "Compiler/Lex/EmojiTokenization.hpp"), encoding="utf-8") as f:
                return result, f.read()

    def test_current_header(self):
        result, hpp = self.run_generator("# emoji-data.txt\n# Date: 2026-01-30\n# Version: 18.0.0\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Unicode Emoji v18.0", hpp)
        self.assertIn("Emoji 18.0: 1 emoji", result.stdout)

    def test_legacy_header(self):
        result, hpp = self.run_generator("# Used with Emoji Version 15.1\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Unicode Emoji v15.1", hpp)

    def test_missing_header(self):
        result, _ = self.run_generator("# nothing here\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("header", result.stderr)
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
