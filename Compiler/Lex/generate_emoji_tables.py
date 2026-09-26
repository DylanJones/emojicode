#!/usr/bin/env python3
"""Generates the emoji tables of the lexer from the Unicode emoji data.

Rewrites isEmoji() and isEmojiModifierBase() in EmojiTokenization.cpp, the version in EmojiTokenization.hpp, and the
emoji-char and emoji-modifier-base rules in docs/grammar.ebnf, which must match them.

    python3 Compiler/Lex/generate_emoji_tables.py [emoji-data.txt]

reads the latest emoji-data.txt from unicode.org if no file is given. Afterwards run
LanguageServer/generate_emoji_names.py and add the new emoji to tests/compilation/identifierTest.emojic.
"""
import os
import re
import sys
import urllib.request

URL = "https://www.unicode.org/Public/UCD/latest/ucd/emoji/emoji-data.txt"
WIDTH = 120

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..", "..")

if len(sys.argv) > 1:
    data = open(sys.argv[1], encoding="utf-8").read()
else:
    data = urllib.request.urlopen(URL).read().decode("utf-8")

version = re.search(r"^# Used with Emoji Version (\d+\.\d+)", data, re.M).group(1)

properties = {}
for line in data.splitlines():
    line = line.split("#", 1)[0].strip()
    if not line:
        continue
    code_points, name = (field.strip() for field in line.split(";"))
    low, _, high = code_points.partition("..")
    properties.setdefault(name, set()).update(range(int(low, 16), int(high or low, 16) + 1))

# #, * and the digits are only emoji in keycap sequences. The lexer does not treat them as emoji.
emoji = properties["Emoji"] - set(map(ord, "#*0123456789"))
modifier_bases = properties["Emoji_Modifier_Base"]

if properties["Emoji_Modifier"] != set(range(0x1F3FB, 0x1F3FF + 1)):
    sys.exit("The emoji modifiers changed. Update isEmojiModifier().")


def ranges(code_points):
    result = []
    for c in sorted(code_points):
        if result and result[-1][1] == c - 1:
            result[-1][1] = c
        else:
            result.append([c, c])
    return result


def wrap(first, indent, terms, separator):
    """Joins @p terms with @p separator into lines of at most WIDTH characters."""
    lines = [first]
    for i, term in enumerate(terms):
        term += separator if i < len(terms) - 1 else ""
        if len(lines[-1]) + len(term) > WIDTH:
            lines[-1] = lines[-1].rstrip()
            lines.append(indent)
        lines[-1] += term
    return "\n".join(lines)


def cpp_body(code_points):
    terms = ["ch == 0x{:04X}".format(low) if low == high else "(0x{:04X} <= ch && ch <= 0x{:04X})".format(low, high)
             for low, high in ranges(code_points)]
    return wrap("    return ", "    ", terms, " || ") + ";"


def ebnf_rule(name, code_points):
    items = ["#x{:X}".format(low) if low == high else "#x{:X}-#x{:X}".format(low, high)
             for low, high in ranges(code_points)]
    first = "{:<19} ::= ".format(name)
    indent = " " * (len(first) - 2) + "| "
    lines, line = [], ""
    for item in items:
        if line and len(indent) + len(line) + len(item) + 2 > WIDTH:
            lines.append(line)
            line = ""
        line += item
    lines.append(line)
    return first + ("\n" + indent).join("[" + line + "]" for line in lines)


def rewrite(path, substitutions):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    for pattern, replacement in substitutions:
        text, count = re.subn(pattern, lambda _: replacement, text, flags=re.S | re.M)
        if count != 1:
            sys.exit("{} does not match in {}".format(pattern, path))
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


rewrite(os.path.join(here, "EmojiTokenization.cpp"), [
    (r"(?<=bool isEmoji\(char32_t ch\) \{\n).*?(?=\n\}\n)", cpp_body(emoji)),
    (r"(?<=bool isEmojiModifierBase\(char32_t ch\) \{\n).*?(?=\n\}\n)", cpp_body(modifier_bases)),
])
rewrite(os.path.join(here, "EmojiTokenization.hpp"), [
    (r"(?<=Unicode Emoji v)[\d.]+", version),
    (r"(?<=https://www\.unicode\.org/Public/)[^/]+(?=/ucd/emoji/emoji-data\.txt)", version + ".0"),
])
rewrite(os.path.join(root, "docs", "grammar.ebnf"), [
    (r"^emoji-char +::=.*?(?=\n\n)", ebnf_rule("emoji-char", emoji)),
    (r"^emoji-modifier-base +::=.*?(?=\n\n)", ebnf_rule("emoji-modifier-base", modifier_bases)),
])
print("Emoji {}: {} emoji, {} modifier bases".format(version, len(emoji), len(modifier_bases)))
