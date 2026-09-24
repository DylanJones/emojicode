#!/usr/bin/env python3
"""Generates emojicode.tmLanguage.json. Edit this file, not the JSON, and run it after changing it.

The keyword lists follow the lexer (Compiler/Lex/Lexer.cpp) and docs/grammar.ebnf. Emoji that the parser recognizes
by the first code point of an identifier are highlighted by their emoji alone. The language server's semantic tokens
refine this highlighting, e.g. to tell method calls and types apart.
"""
import json
import os

V = r'\x{FE0F}?'
EMOJI = r'(?:\p{Extended_Pictographic}|[\x{1F1E6}-\x{1F1FF}])'
# Emoji with a meaning of their own, which never start an identifier that names a type or method.
RESERVED = ('❗❓⁉🤜🤛↩🔁🔂👍👎🚨↪🆗🍇🍉🆕👇☣⤴➡⬅🖍🐚🐊🕊🐇🔘📣🍿➕➖➗✖👐🤝⭕💢❌👈👉🚮🙌😜🔤🧲💭📗📘🔟◀▶🎍🍆'
            '🔶🍬✴🍱🚧🍺🔺🔲⚖🏮🙅🤷◼⚪🔵⚫⬛🌍🔏✒🥯⚠🔑🔓🔒🔐📻🍼♻📦📜🔗🏁')
IDENT = ('(?![' + RESERVED + '])' + EMOJI + r'[\x{FE0F}\x{1F3FB}-\x{1F3FF}\x{1F1E6}-\x{1F1FF}]*'
         r'(?:[\x{200D}🔸][\x{FE0F}\x{200D}]*' + EMOJI + r'[\x{FE0F}\x{1F3FB}-\x{1F3FF}]*)*')
NOT_VAR = r'\s\p{Extended_Pictographic}\x{FE0F}\x{200D}'
MODIFIERS = ['🌍', '🔏', '✒', '🥯', '⚠', '🔑', '☣', '🖍', '🔓', '🔒', '🔐', '📻', '🍼']


def any_of(emoji):
    return '(?:' + '|'.join(emoji) + ')' + V


def delimited(name, begin, end=None, **rest):
    return {"name": name, "begin": begin, "end": end or begin,
            "beginCaptures": {"0": {"name": "punctuation.definition.comment.begin.emojicode"}},
            "endCaptures": {"0": {"name": "punctuation.definition.comment.end.emojicode"}}, **rest}


grammar = {
    "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
    "name": "Emojicode",
    "scopeName": "source.emojicode",
    "patterns": [{"include": "#code"}],
    "repository": {
        "code": {"patterns": [{"include": "#" + name} for name in
                              ("comments", "strings", "declarations", "keywords", "numbers", "variables")]},
        "comments": {"patterns": [
            delimited("comment.block.emojicode", "💭🔜", "🔚💭"),
            {"name": "comment.line.emojicode", "match": "(💭).*$",
             "captures": {"1": {"name": "punctuation.definition.comment.emojicode"}}},
            delimited("comment.block.documentation.emojicode", "📗"),
            delimited("comment.block.documentation.package.emojicode", "📘"),
        ]},
        "strings": {"patterns": [
            {"name": "string.quoted.emojicode", "begin": "🔤", "end": "🔤",
             "beginCaptures": {"0": {"name": "punctuation.definition.string.begin.emojicode"}},
             "endCaptures": {"0": {"name": "punctuation.definition.string.end.emojicode"}},
             "patterns": [
                 {"name": "constant.character.escape.emojicode", "match": "❌[ntr❌🔤🧲]"},
                 {"name": "invalid.illegal.escape.emojicode", "match": "❌."},
                 {"name": "meta.interpolation.emojicode", "contentName": "source.emojicode.embedded",
                  "begin": "🧲", "end": "🧲",
                  "beginCaptures": {"0": {"name": "punctuation.section.interpolation.begin.emojicode"}},
                  "endCaptures": {"0": {"name": "punctuation.section.interpolation.end.emojicode"}},
                  "patterns": [{"include": "#code"}]},
             ]},
            {"name": "constant.other.symbol.emojicode", "match": "🔟."},
        ]},
        "declarations": {"patterns": [
            {"comment": "Method definitions start a line, after their attributes: ❗ 🐽, ❓ 🐽 or ➡ 🐽.",
             "match": r"^\s*((?:(?:" + '|'.join(MODIFIERS + ['🐇', '🎍.']) + ")" + V + r"\s*)*)(" +
                      any_of(['❗', '❓', '➡']) + r")\s*(" + IDENT + ")",
             "captures": {"1": {"patterns": [{"name": "storage.modifier.static.emojicode", "match": any_of(['🐇'])},
                                             {"include": "#keywords"}]},
                          "2": {"name": "storage.type.function.emojicode"},
                          "3": {"name": "entity.name.function.emojicode"}}},
            {"comment": "Type definitions: 🐇 🍨, 🕊 🍨, 🔘 🍨, 🐊 🍨. Also matches type values, which are types too.",
             "match": "(" + any_of(['🐇', '🕊', '🔘', '🐊']) + r")\s*(?:(🔶" + V + r")\s*(" + IDENT + r")\s*)?(" +
                      IDENT + ")",
             "captures": {"1": {"name": "storage.type.emojicode"},
                          "2": {"name": "punctuation.accessor.namespace.emojicode"},
                          "3": {"name": "entity.name.namespace.emojicode"},
                          "4": {"name": "entity.name.type.emojicode"}}},
            {"comment": "Named initializers: 🆕 ▶ 🐽.",
             "match": "(🆕" + V + r")\s*(▶" + V + r")\s*(" + IDENT + ")",
             "captures": {"1": {"name": "keyword.other.new.emojicode"},
                          "2": {"name": "storage.type.function.emojicode"},
                          "3": {"name": "entity.name.function.emojicode"}}},
            {"comment": "A type in a namespace: 🔶🌊🔢.",
             "match": "(🔶" + V + r")\s*(" + IDENT + r")\s*(" + IDENT + ")",
             "captures": {"1": {"name": "punctuation.accessor.namespace.emojicode"},
                          "2": {"name": "entity.name.namespace.emojicode"},
                          "3": {"name": "entity.name.type.emojicode"}}},
        ]},
        "keywords": {"patterns": [
            {"name": "meta.decorator.emojicode", "match": "(🎍)(" + EMOJI + V + ")",
             "captures": {"1": {"name": "punctuation.definition.decorator.emojicode"},
                          "2": {"name": "storage.modifier.decorator.emojicode"}}},
            {"name": "keyword.control.emojicode",
             "match": "🙅" + V + "↪" + V + "|" + any_of(['↩', '🔁', '🔂', '↪', '🙅', '🆗', '🚨'])},
            {"name": "keyword.other.import.emojicode", "match": any_of(['📦', '📜', '🔗'])},
            {"name": "keyword.other.start.emojicode", "match": any_of(['🏁'])},
            {"name": "keyword.other.new.emojicode", "match": any_of(['🆕'])},
            {"name": "keyword.other.deinitializer.emojicode", "match": any_of(['♻'])},
            {"name": "storage.type.emojicode", "match": any_of(['🐇', '🕊', '🔘', '🐊'])},
            {"name": "storage.modifier.emojicode", "match": any_of(MODIFIERS)},
            {"name": "variable.language.this.emojicode", "match": any_of(['👇'])},
            {"name": "variable.language.super.emojicode", "match": any_of(['⤴'])},
            {"name": "constant.language.boolean.emojicode", "match": any_of(['👍', '👎'])},
            {"name": "constant.language.novalue.emojicode", "match": any_of(['🤷'])},
            {"name": "support.type.builtin.emojicode", "match": any_of(['◼', '⚪', '🔵', '⚫'])},
            {"name": "keyword.operator.type.emojicode", "match": any_of(['🍬', '✴', '⬛', '🚧', '🍱'])},
            {"name": "keyword.operator.emojicode", "match": "[◀▶]" + V + "(?:🙌" + V + ")?|" + any_of(
                ['➕', '➖', '➗', '✖', '👐', '🤝', '⭕', '💢', '❌', '👈', '👉', '🚮', '🙌', '😜',
                 '🍺', '🔺', '🔲', '⚖', '🏮', '📣', '⁉'])},
            {"name": "keyword.operator.assignment.emojicode", "match": any_of(['➡', '⬅'])},
            {"name": "punctuation.terminator.mood.emojicode", "match": any_of(['❗', '❓'])},
            {"name": "punctuation.section.block.begin.emojicode", "match": "🍇"},
            {"name": "punctuation.section.block.end.emojicode", "match": "🍉"},
            {"name": "punctuation.section.group.begin.emojicode", "match": "🤜"},
            {"name": "punctuation.section.group.end.emojicode", "match": "🤛"},
            {"name": "punctuation.definition.generic.begin.emojicode", "match": "🐚"},
            {"name": "punctuation.definition.collection.begin.emojicode", "match": "🍿"},
            {"name": "punctuation.definition.generic.end.emojicode", "match": "🍆"},
        ]},
        "numbers": {"patterns": [
            {"name": "constant.numeric.emojicode",
             "match": "(?<![^" + NOT_VAR + r"])(?:0,*[xX][0-9a-fA-F,]*|[+\-]?,*[0-9][0-9,]*)(?:\.[0-9]+)?(?![^" +
                      NOT_VAR + "])"},
        ]},
        "variables": {"patterns": [
            {"name": "variable.other.emojicode", "match": "[^" + NOT_VAR + r"0-9+\-][^" + NOT_VAR + "]*"},
        ]},
    },
}

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'emojicode.tmLanguage.json')
with open(path, 'w') as f:
    json.dump(grammar, f, ensure_ascii=False, indent=2)
    f.write('\n')
