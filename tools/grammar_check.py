#!/usr/bin/env python3
"""Checks that docs/grammar.ebnf describes the language that the compiler accepts.

The grammar is read and interpreted directly: there is no hand-written copy of it here. The checks are:

  1. Well-formedness: every rule that is referenced is defined, every rule is used, there is no left recursion, and
     the lexical and syntactic levels are kept apart.
  2. Tables: the keywords, the characters that start special tokens, the emoji and whitespace tables, and the
     binary operators with their precedence are compared with the compiler's C++ source.
  3. Corpus: every source file that the build and the test suite compile is tokenized with the grammar and with the
     compiler (emojicodec --dump-tokens), and the tokens must be identical. The grammar must accept every file.
  4. Reject tests: tests/reject/*.emojic are parsed with emojicodec --parse-only. If the compiler's parser accepts a
     file, the grammar must accept it too. If the parser rejects it, the grammar must reject it too, unless the
     error is one that the grammar does not describe (see "Scope" in the grammar).
  5. Mutations (--mutations N): N variants of each corpus file are made by deleting, duplicating, swapping or
     replacing tokens, or by inserting or deleting single characters. The grammar and emojicodec --parse-only must
     agree on each variant, as in check 4. For character mutations, the tokens must also be identical, as in check 3.

  6. Generation (--generate N): N random documents are generated from the syntactic grammar, choosing the least
     used alternative of each rule so that all of them are exercised. The compiler's parser must accept each one,
     unless it reports only errors outside the grammar.

Checks 3 and 4 test that the grammar accepts everything that the compiler accepts, and check 6 tests the converse.
Check 5 tests both. Check 1 and 2 need only the source tree. The others need a build directory (--build).
"""

import argparse
import bisect
import concurrent.futures
import glob
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRAMMAR_PATH = os.path.join(ROOT, 'docs', 'grammar.ebnf')

# Rules that the tokenization procedure and the syntactic grammar start from.
LEXICAL_ROOTS = ['ignorable', 'token-shape', 'token']
SYNTAX_ROOT = 'document'

# Compiler errors that depend on more than the document and are therefore not described by the grammar.
CONTEXT_ERRORS = [
    'is already defined',
    'Duplicate 🏁',
    'Link hints were already provided',
    'Could not find package',
    'Circular dependency',
    'could not be loaded into namespace',
    "Couldn't read input file",
    'Emojicode files must',
    'was already exported',
    'Duplicate enum value',
    'same name is already in use',
    'Redeclaration of instance variable',
    'overload is declared twice',
    'initialized with 🍼 more than once',
    's package',
    'is not a known C type',
]

# Directories whose sources are packages. A mutated file from one of them is parsed together with the rest of its
# package, as the s package in particular cannot be parsed one file at a time.
PACKAGES = ['s', 'files', 'json', 'sockets', 'testtube', 'c']

CORPUS_PATTERNS = [
    'tests/compilation/*.emojic',
    'tests/s/*.emojic',
    's/*.🍇',
    'files/*.🍇',
    'json/*.🍇',
    'sockets/*.🍇',
    'testtube/*.🍇',
    'c/*.🍇',
]
REJECT_PATTERN = 'tests/reject/*.emojic'

# Characters that character mutations insert, as they are significant to the lexer.
LEXICAL_CHARACTERS = list('0123456789+-.,xXaF \n') + ['\u200d', '\ufe0f', '\u2028', '🔸', '❌', '🔤', '🧲', '💭', '🔜',
                                                        '🔚', '📗', '📘', '🔟', '🎍', '◀', '▶', '🙌', '🙅', '↪', '🤷',
                                                        '🇩', '🏻', '👍', '🍉', '🍇']
WHITESPACE = ' \t\n\r\u2028\u2029\ufe0f'


class GrammarError(Exception):
    pass


# ---------------------------------------------------------------------------------------------------------------------
# Reading the grammar
#
# Expressions are tuples: ('lit', string), ('set', ranges, negated), ('ref', name), ('seq', [e]), ('alt', [e]),
# ('diff', a, b), ('opt', e), ('star', e), ('plus', e).
# ---------------------------------------------------------------------------------------------------------------------

TOKEN_RE = re.compile(r"""
    (?P<space>\s+)
  | (?P<define>::=)
  | (?P<name>[a-z][a-z0-9-]*[a-z0-9]|[a-z])
  | (?P<string>'[^']*'|"[^"]*")
  | (?P<hex>\#x[0-9A-Fa-f]+)
  | (?P<set>\[\^?[^\]]+\])
  | (?P<punct>[()|?*+-])
""", re.X)


class Grammar:
    def __init__(self, path):
        text = open(path, encoding='utf-8').read()
        marker = re.search(r'@syntax[ \t]*$', text, re.M)
        if not marker:
            raise GrammarError('The heading of the first syntactic section must end with @syntax.')
        syntax_start = marker.start()
        text = re.sub(r'/\*.*?\*/', lambda m: ' ' * len(m.group()), text, flags=re.S)

        tokens = []
        pos = 0
        while pos < len(text):
            m = TOKEN_RE.match(text, pos)
            if not m:
                raise GrammarError('Unexpected character {!r} at {}'.format(text[pos], line_of(text, pos)))
            if m.lastgroup != 'space':
                tokens.append((m.lastgroup, m.group(), pos))
            pos = m.end()

        self.rules = {}
        self.lexical = set()
        self.order = []
        self.lines = {}
        self._tokens = tokens
        self._i = 0
        while self._i < len(tokens):
            kind, name, pos = tokens[self._i]
            if kind != 'name' or self._peek(1) != 'define':
                raise GrammarError('Expected a rule definition at ' + line_of(text, pos))
            self._i += 2
            if name in self.rules:
                raise GrammarError('Rule {} is defined twice.'.format(name))
            self.rules[name] = self._alt()
            self.order.append(name)
            self.lines[name] = line_of(text, pos)
            if pos < syntax_start:
                self.lexical.add(name)

    def _peek(self, offset=0):
        i = self._i + offset
        return self._tokens[i][0] if i < len(self._tokens) else None

    def _at_rule_start(self):
        return self._peek() == 'name' and self._peek(1) == 'define'

    def _next(self):
        token = self._tokens[self._i]
        self._i += 1
        return token

    def _alt(self):
        items = [self._diff()]
        while self._peek() == 'punct' and self._tokens[self._i][1] == '|':
            self._i += 1
            items.append(self._diff())
        return items[0] if len(items) == 1 else ('alt', items)

    def _diff(self):
        left = self._seq()
        if self._peek() == 'punct' and self._tokens[self._i][1] == '-':
            self._i += 1
            return ('diff', left, self._seq())
        return left

    def _seq(self):
        items = []
        while self._i < len(self._tokens) and not self._at_rule_start():
            kind, value, _ = self._tokens[self._i]
            if kind == 'punct' and value in '|-)':
                break
            items.append(self._postfix())
        if not items:
            raise GrammarError('Empty expression near token {}'.format(self._i))
        return items[0] if len(items) == 1 else ('seq', items)

    def _postfix(self):
        expr = self._primary()
        while self._peek() == 'punct' and self._tokens[self._i][1] in '?*+':
            op = self._next()[1]
            expr = ({'?': 'opt', '*': 'star', '+': 'plus'}[op], expr)
        return expr

    def _primary(self):
        kind, value, _ = self._next()
        if kind == 'name':
            return ('ref', value)
        if kind == 'string':
            return ('lit', value[1:-1])
        if kind == 'hex':
            return ('lit', chr(int(value[2:], 16)))
        if kind == 'set':
            return parse_set(value)
        if kind == 'punct' and value == '(':
            expr = self._alt()
            if self._next()[1] != ')':
                raise GrammarError('Expected )')
            return expr
        raise GrammarError('Unexpected {!r}'.format(value))


def line_of(text, pos):
    return 'line {}'.format(text.count('\n', 0, pos) + 1)


def parse_set(text):
    negated = text.startswith('[^')
    body = text[2 if negated else 1:-1]
    ranges = []
    i = 0

    def atom():
        nonlocal i
        m = re.match(r'#x([0-9A-Fa-f]+)', body[i:])
        if m:
            i += m.end()
            return int(m.group(1), 16)
        i += 1
        return ord(body[i - 1])

    while i < len(body):
        lo = atom()
        hi = lo
        if i < len(body) - 1 and body[i] == '-':
            i += 1
            hi = atom()
        ranges.append((lo, hi))
    return ('set', normalize_ranges(ranges), negated)


def normalize_ranges(ranges):
    merged = []
    for lo, hi in sorted(ranges):
        if merged and lo <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(hi, merged[-1][1]))
        else:
            merged.append((lo, hi))
    return merged


def in_ranges(ranges, c):
    i = bisect.bisect_right(ranges, (c, sys.maxsize)) - 1
    return i >= 0 and ranges[i][0] <= c <= ranges[i][1]


def walk(expr):
    yield expr
    if expr[0] in ('seq', 'alt'):
        for item in expr[1]:
            yield from walk(item)
    elif expr[0] == 'diff':
        yield from walk(expr[1])
        yield from walk(expr[2])
    elif expr[0] in ('opt', 'star', 'plus'):
        yield from walk(expr[1])


# ---------------------------------------------------------------------------------------------------------------------
# Matching
#
# ends(expr, i) returns the set of positions at which a match of expr starting at i can end. To keep "anything"
# patterns like char* and token* cheap, a set of ends is a pair (points, lo): the positions in points, plus every
# position from lo to the end of the input if lo is not None.
# ---------------------------------------------------------------------------------------------------------------------

NO_ENDS = (frozenset(), None)


def union(a, b):
    lo = a[1] if b[1] is None else b[1] if a[1] is None else min(a[1], b[1])
    points = a[0] | b[0]
    if lo is not None:
        points = frozenset(p for p in points if p < lo)
    return points, lo


def gather(all_ends):
    points = set()
    lo = None
    for p, rest in all_ends:
        points |= p
        if rest is not None:
            lo = rest if lo is None else min(lo, rest)
    if lo is not None:
        points = {p for p in points if p < lo}
    return frozenset(points), lo


def positions(ends, n):
    yield from sorted(ends[0])
    if ends[1] is not None:
        yield from range(ends[1], n + 1)


def contains(ends, j):
    return j in ends[0] or (ends[1] is not None and j >= ends[1])


def last(ends, n):
    if ends[1] is not None:
        return n
    return max(ends[0]) if ends[0] else None


class LeftRecursion(Exception):
    pass


class Matcher:
    """Matches expressions against a string (lexical) or a list of tokens (syntactic)."""

    def __init__(self, grammar, items, syntactic, text_matcher=None):
        self.g = grammar
        self.items = items
        self.n = len(items)
        self.syntactic = syntactic
        self.text_matcher = text_matcher
        self.memo = {}
        self.active = set()

    def ends(self, expr, i):
        kind = expr[0]
        if kind == 'ref':
            return self.ref(expr[1], i)
        if kind == 'lit':
            if self.syntactic:
                return (frozenset([i + 1]), None) if i < self.n and literal_matches(expr[1], self.items[i]) \
                    else NO_ENDS
            return (frozenset([i + len(expr[1])]), None) if self.items.startswith(expr[1], i) else NO_ENDS
        if kind == 'set':
            if self.syntactic:
                raise GrammarError('Character set used in the syntactic grammar.')
            if i < self.n and in_ranges(expr[1], ord(self.items[i])) != expr[2]:
                return frozenset([i + 1]), None
            return NO_ENDS
        if kind == 'seq':
            current = (frozenset([i]), None)
            for item in expr[1]:
                current = gather(self.ends(item, p) for p in positions(current, self.n))
                if current == NO_ENDS:
                    break
            return current
        if kind == 'alt':
            return gather(self.ends(item, i) for item in expr[1])
        if kind == 'diff':
            a = self.ends(expr[1], i)
            if a == NO_ENDS:
                return a
            b = self.ends(expr[2], i)
            return frozenset(p for p in positions(a, self.n) if not contains(b, p)), None
        if kind == 'opt':
            return union((frozenset([i]), None), self.ends(expr[1], i))
        if kind in ('star', 'plus'):
            if expr[1] == ('ref', 'token' if self.syntactic else 'char'):
                # Matches anything: every position from here (or the next one) to the end.
                start = i if kind == 'star' else i + 1
                return (frozenset(), start) if start <= self.n else NO_ENDS
            found = {i} if kind == 'star' else set()
            lo = None
            frontier = [i]
            while frontier:
                new = []
                for p in frontier:
                    points, rest = self.ends(expr[1], p)
                    if rest is not None:
                        lo = rest if lo is None else min(lo, rest)
                    for q in points:
                        if q not in found:
                            found.add(q)
                            new.append(q)
                frontier = [q for q in new if lo is None or q < lo]
            return frozenset(p for p in found if lo is None or p < lo), lo
        raise GrammarError('Unknown expression ' + kind)

    def ref(self, name, i):
        if self.syntactic and name in self.g.lexical:
            if i < self.n and (name == 'token' or self.text_matcher.full(name, self.items[i].text)):
                return frozenset([i + 1]), None
            return NO_ENDS
        key = (name, i)
        if key in self.memo:
            return self.memo[key]
        if key in self.active:
            raise LeftRecursion(name)
        if name not in self.g.rules:
            raise GrammarError('Undefined rule ' + name)
        self.active.add(key)
        try:
            result = self.ends(self.g.rules[name], i)
        finally:
            self.active.discard(key)
        self.memo[key] = result
        return result


class TextMatcher:
    """Whether a whole string matches a lexical rule, with a cache."""

    def __init__(self, grammar):
        self.g = grammar
        self.cache = {}

    def full(self, name, text):
        key = (name, text)
        if key not in self.cache:
            matcher = Matcher(self.g, text, False)
            self.cache[key] = contains(matcher.ref(name, 0), len(text))
        return self.cache[key]


def render(e):
    kind = e[0]
    if kind == 'lit':
        return "'{}'".format(e[1])
    if kind == 'ref':
        return e[1]
    if kind == 'set':
        return '[...]'
    if kind == 'seq':
        return ' '.join(render(x) if x[0] != 'alt' else '( {} )'.format(render(x)) for x in e[1])
    if kind == 'alt':
        return ' | '.join(render(x) for x in e[1])
    if kind == 'diff':
        return '{} - {}'.format(render(e[1]), render(e[2]))
    inner = render(e[1])
    if e[1][0] in ('seq', 'alt', 'diff'):
        inner = '( {} )'.format(inner)
    return inner + {'opt': '?', 'star': '*', 'plus': '+'}[kind]


def strip_fe0f(text):
    return text.replace('️', '')


def literal_matches(literal, token):
    if strip_fe0f(token.text) == literal:
        return True
    return len(literal) == 1 and token.kind == 'identifier' and token.text[0] == literal


# ---------------------------------------------------------------------------------------------------------------------
# Tokenization
# ---------------------------------------------------------------------------------------------------------------------

class Token:
    def __init__(self, kind, text, start):
        self.kind = kind
        self.text = text
        self.start = start

    def __repr__(self):
        return '{}({!r})@{}'.format(self.kind, self.text, self.start)


class LexError(Exception):
    def __init__(self, position, message):
        super().__init__('at code point {}: {}'.format(position, message))
        self.position = position


class Lexer:
    def __init__(self, grammar):
        self.g = grammar
        self.text = TextMatcher(grammar)
        body = grammar.rules['token']
        self.kinds = [item[1] for item in (body[1] if body[0] == 'alt' else [body])]
        if any(item[0] != 'ref' for item in (body[1] if body[0] == 'alt' else [body])):
            raise GrammarError('Each alternative of token must be a rule name.')

    def lex(self, source, keep_comments=False):
        matcher = Matcher(self.g, source, False)
        n = len(source)
        tokens = []
        i = 0
        while i < n:
            end = last(matcher.ref('ignorable', i), n)
            if end is not None and end > i:
                if keep_comments and end - i > 1:
                    for kind in ('single-line-comment', 'multi-line-comment'):
                        if self.text.full(kind, source[i:end]):
                            tokens.append(Token(kind, source[i:end], i))
                i = end
                continue
            end = last(matcher.ref('token-shape', i), n)
            if end is None or end == i:
                raise LexError(i, 'no token starts with {!r}'.format(source[i]))
            text = source[i:end]
            kinds = [kind for kind in self.kinds if self.text.full(kind, text)]
            if len(kinds) != 1:
                raise LexError(i, '{!r} matches {} kinds of token: {}'.format(text, len(kinds), kinds))
            tokens.append(Token(kinds[0], text, i))
            i = end
        return tokens


def parses(grammar, text_matcher, tokens):
    """Returns None if the tokens match document, or the index of the first token that could not be matched."""
    matcher = Matcher(grammar, tokens, True, text_matcher)
    ends = matcher.ref(SYNTAX_ROOT, 0)
    if contains(ends, len(tokens)):
        return None
    return max((max(positions(ends, len(tokens))) for ends in matcher.memo.values() if ends != NO_ENDS), default=0)


def run_deep(function, *args):
    """Runs function in a thread with a large stack, as matching recurses deeply on nested code."""
    result = {}

    def target():
        try:
            result['value'] = function(*args)
        except BaseException as e:
            result['error'] = e

    thread = threading.Thread(target=target)
    thread.start()
    thread.join()
    if 'error' in result:
        raise result['error']
    return result['value']


# ---------------------------------------------------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------------------------------------------------

class Report:
    def __init__(self):
        self.failures = 0
        self.warnings = 0

    def section(self, title):
        print('\n== ' + title)

    def ok(self, message):
        print('  ✅ ' + message)

    def fail(self, message):
        self.failures += 1
        print('  ❌ ' + message)

    def warn(self, message):
        self.warnings += 1
        print('  ⚠️  ' + message)


def check_well_formed(g, report):
    report.section('Well-formedness')
    problems = []
    for name in g.order:
        for expr in walk(g.rules[name]):
            if expr[0] == 'ref' and expr[1] not in g.rules:
                problems.append('{} ({}) refers to undefined rule {}'.format(name, g.lines[name], expr[1]))
            if expr[0] == 'ref' and name in g.lexical and expr[1] not in g.lexical and expr[1] in g.rules:
                problems.append('lexical rule {} refers to syntactic rule {}'.format(name, expr[1]))
            if name not in g.lexical and expr[0] == 'set':
                problems.append('syntactic rule {} uses a character set'.format(name))

    reachable = set()
    todo = LEXICAL_ROOTS + [SYNTAX_ROOT]
    while todo:
        name = todo.pop()
        if name in reachable or name not in g.rules:
            continue
        reachable.add(name)
        todo.extend(e[1] for e in walk(g.rules[name]) if e[0] == 'ref')
    for name in g.order:
        if name not in reachable:
            problems.append('rule {} ({}) is not used'.format(name, g.lines[name]))

    problems.extend(left_recursion(g))
    for problem in problems:
        report.fail(problem)
    if not problems:
        report.ok('{} rules ({} lexical, {} syntactic), all defined, used and free of left recursion'.format(
            len(g.rules), len(g.lexical), len(g.rules) - len(g.lexical)))


def left_recursion(g):
    nullable = set()
    changed = True

    def is_nullable(e):
        kind = e[0]
        if kind in ('opt', 'star'):
            return True
        if kind == 'ref':
            return e[1] in nullable
        if kind == 'seq':
            return all(is_nullable(x) for x in e[1])
        if kind == 'alt':
            return any(is_nullable(x) for x in e[1])
        if kind in ('plus', 'diff'):
            return is_nullable(e[1])
        return False

    while changed:
        changed = False
        for name, body in g.rules.items():
            if name not in nullable and is_nullable(body):
                nullable.add(name)
                changed = True

    def leftmost(e):
        kind = e[0]
        if kind == 'ref':
            return {e[1]}
        if kind == 'seq':
            refs = set()
            for x in e[1]:
                refs |= leftmost(x)
                if not is_nullable(x):
                    break
            return refs
        if kind == 'alt':
            return set().union(*(leftmost(x) for x in e[1]))
        if kind == 'diff':
            return leftmost(e[1]) | leftmost(e[2])
        if kind in ('opt', 'star', 'plus'):
            return leftmost(e[1])
        return set()

    graph = {name: leftmost(body) & set(g.rules) for name, body in g.rules.items()}
    problems = []
    for start in g.rules:
        seen = set()
        todo = list(graph[start])
        while todo:
            name = todo.pop()
            if name == start:
                problems.append('rule {} is left-recursive'.format(start))
                break
            if name not in seen:
                seen.add(name)
                todo.extend(graph[name])
    return problems


def read_source(*parts):
    return open(os.path.join(ROOT, *parts), encoding='utf-8').read()


def emoji_constants():
    constants = {}
    for name, value in re.findall(r"(E_[A-Z0-9_]+) = (0x[0-9A-Fa-f]+|U'.')", read_source('Compiler', 'Emojis.h')):
        constants[name] = int(value, 16) if value.startswith('0x') else ord(value[2])
    return constants


def cpp_code_point(expression, constants):
    expression = expression.strip()
    if expression.startswith("U'"):
        return ord(expression[2])
    if expression.startswith('0x'):
        return int(expression, 16)
    return constants[expression]


def cpp_ranges(source, function, variable):
    body = re.search(r'bool ' + function + r'\([^)]*\)( const)? \{(.*?)\}', source, re.S).group(2)
    v = re.escape(variable)
    ranges = [(int(a, 16), int(b, 16)) for a, b in
              re.findall(r'0x([0-9A-Fa-f]+) <= ' + v + r' && ' + v + r' <= 0x([0-9A-Fa-f]+)', body)]
    ranges += [(int(a, 16),) * 2 for a in re.findall(v + r' == 0x([0-9A-Fa-f]+)', body)]
    return normalize_ranges(ranges)


def grammar_set(g, name):
    """The code points matched by a lexical rule made of single-character alternatives."""
    ranges = []
    for e in walk(g.rules[name]):
        if e[0] == 'set':
            if e[2]:
                raise GrammarError(name + ' uses a negated set')
            ranges.extend(e[1])
        elif e[0] == 'lit':
            ranges.append((ord(e[1]),) * 2)
    return normalize_ranges(ranges)


def literals(g, name, follow=False):
    result = set()
    for e in walk(g.rules[name]):
        if e[0] == 'lit':
            result.add(e[1])
        elif e[0] == 'ref' and follow:
            result |= literals(g, e[1], follow)
    return result


def token_type_names():
    names = dict(re.findall(r'case TokenType::(\w+): return "([^"]+)";', read_source('Compiler', 'Lex', 'Token.cpp')))
    return names


def compiler_keywords(constants):
    lexer = read_source('Compiler', 'Lex', 'Lexer.cpp')
    return {cpp_code_point(cp, constants): kind
            for cp, kind in re.findall(r'singleTokens_\.emplace\(([^,]+),\s*TokenType::(\w+)\)', lexer)}


def check_tables(g, report):
    report.section('Tables in the compiler source')
    constants = emoji_constants()
    lexer = read_source('Compiler', 'Lex', 'Lexer.cpp')

    keywords = compiler_keywords(constants)
    grammar_keywords = {ord(c) for c in literals(g, 'keyword')}
    compare_sets(report, 'keywords', {chr(c) for c in keywords}, {chr(c) for c in grammar_keywords})

    begin = re.search(r'bool Lexer::beginToken.*?switch \(codePoint\(\)\) \{(.*?)default:', lexer, re.S).group(1)
    special = {chr(constants[name]) for name in re.findall(r'case (E_\w+):', begin)}
    compare_sets(report, 'code points that start a special token', special,
                 literals(g, 'not-identifier-start') - literals(g, 'keyword'))

    tokenization = read_source('Compiler', 'Lex', 'EmojiTokenization.cpp')
    for function, rule in [('isEmoji', 'emoji-char'), ('isEmojiModifierBase', 'emoji-modifier-base'),
                           ('isEmojiModifier', 'emoji-modifier'), ('isRegionalIndicator', 'regional-indicator')]:
        compare_ranges(report, rule, cpp_ranges(tokenization, function, 'ch'), grammar_set(g, rule))

    lexer_header = read_source('Compiler', 'Lex', 'Lexer.hpp')
    for function, rule in [('isWhitespace', 'whitespace'), ('isNewline', 'newline')]:
        compare_ranges(report, rule, cpp_ranges(lexer_header, function, 'codePoint()'), grammar_set(g, rule))

    check_operators(g, report, constants, keywords, special)


def compare_sets(report, what, expected, actual):
    if expected == actual:
        report.ok('{}: {} entries match'.format(what, len(expected)))
    else:
        report.fail('{}: compiler only: {}  grammar only: {}'.format(
            what, ' '.join(sorted(expected - actual)) or '-', ' '.join(sorted(actual - expected)) or '-'))


def compare_ranges(report, what, expected, actual):
    if expected == actual:
        report.ok('{}: {} ranges match'.format(what, len(expected)))
    else:
        def fmt(ranges):
            return ' '.join('{:X}-{:X}'.format(*r) for r in ranges[:8])
        report.fail('{}: compiler {} ... grammar {} ...'.format(what, fmt(expected), fmt(actual)))


def check_operators(g, report, constants, keywords, special):
    helper = read_source('Compiler', 'Parsing', 'OperatorHelper.cpp')
    names = {}
    for kind, value in re.findall(r'case OperatorType::(\w+):\s*return (std::u32string\(1, E_\w+\)|\{[^}]*\});',
                                  helper):
        cps = re.findall(r'E_\w+', value)
        names[kind] = ''.join(chr(constants[c]) for c in cps)
    precedence_body = re.search(r'int operatorPrecedence.*?\{(.*?)\n\}', helper, re.S).group(1)
    precedence = {}
    pending = []
    for line in precedence_body.splitlines():
        m = re.search(r'case OperatorType::(\w+):', line)
        if m:
            pending.append(m.group(1))
        m = re.search(r'return (\d+);', line)
        if m:
            for kind in pending:
                precedence[kind] = int(m.group(1))
            pending = []
    levels = {}
    for kind, level in precedence.items():
        levels.setdefault(level, set()).add(names[kind])
    expected = [levels[level] for level in sorted(levels)]

    actual = []
    name = 'expression'
    while True:
        body = g.rules[name]
        if body[0] == 'ref':
            name = body[1]
            continue
        if body[0] == 'seq' and len(body[1]) == 2 and body[1][0][0] == 'ref' and body[1][1][0] == 'star':
            operand = body[1][0][1]
            repeated = body[1][1][1]
            if repeated[0] == 'seq' and repeated[1][-1] == ('ref', operand):
                ops = {e[1] for e in walk(repeated[1][0]) if e[0] == 'lit'}
                actual.append(ops)
                name = operand
                continue
        break
    if expected == actual:
        report.ok('binary operators: {} precedence levels match'.format(len(expected)))
    else:
        report.fail('binary operators: compiler (loosest first) {} grammar {}'.format(
            [' '.join(sorted(s)) for s in expected], [' '.join(sorted(s)) for s in actual]))

    all_operators = set(names.values())
    single = {chr(cp) for cp, kind in keywords.items() if kind == 'Operator'}
    triangles = {c for c in special if any(op.startswith(c) for op in all_operators)}
    lexed = single | triangles | {t + '🙌' for t in triangles}
    compare_sets(report, 'operator tokens', all_operators, lexed)

    undefinable = set(re.search(r'bool canOperatorBeDefined.*?return (.*?);', helper, re.S).group(1).split('&&'))
    undefinable = {names[re.search(r'OperatorType::(\w+)', x).group(1)] for x in undefinable}
    compare_sets(report, 'operators that cannot be defined', undefinable,
                 {e[1] for e in walk(g.rules['definable-operator'][2]) if e[0] == 'lit'})


def check_literals(g, lexer, report):
    """Every quoted terminal in the syntactic grammar must be exactly one token."""
    problems = []
    for name in g.order:
        if name in g.lexical:
            continue
        for e in walk(g.rules[name]):
            if e[0] == 'lit':
                try:
                    tokens = lexer.lex(e[1] + '\n')
                except LexError as error:
                    problems.append('{} in {}: {}'.format(e[1], name, error))
                    continue
                if len(tokens) != 1 or tokens[0].text != e[1]:
                    problems.append('{} in {} is not a single token: {}'.format(e[1], name, tokens))
    for problem in problems:
        report.fail(problem)
    if not problems:
        report.ok('every quoted terminal in the syntactic grammar is a single token')


# ---- Checks that use the compiler ----

def compiler_tokens(emojicodec, path):
    completed = subprocess.run([emojicodec, '--dump-tokens', path], stdout=subprocess.PIPE, timeout=60)
    tokens = []
    error = None
    for line in completed.stdout.decode('utf-8').splitlines():
        start, kind, value = line.split('\t')
        if start == 'error':
            error = value
            break
        if kind in ('Line Break', 'BlankLine'):
            continue
        tokens.append((int(start), kind, ''.join(chr(int(c, 16)) for c in value.split())))
    return tokens, error


def unescape(text):
    escapes = {'n': '\n', 't': '\t', 'r': '\r'}
    return re.sub('❌(.)', lambda m: escapes.get(m.group(1), m.group(1)), text, flags=re.S)


class TokenComparison:
    def __init__(self):
        constants = emoji_constants()
        names = token_type_names()
        self.keywords = {chr(cp): names[kind] for cp, kind in compiler_keywords(constants).items()}
        self.types = {
            'triangle-operator': 'Operator', 'identifier': 'Identifier', 'no-value': 'NoValue', 'else': 'Else',
            'else-if': 'ElseIf', 'variable': 'Variable', 'integer-literal': 'Integer', 'float-literal': 'Double',
            'string-literal': 'String', 'interpolation-begin': 'BeginInterpolation',
            'interpolation-middle': 'MiddleInterpolation', 'interpolation-end': 'EndInterpolation',
            'decorator': 'Decorator', 'symbol-literal': 'Symbol', 'documentation-comment': names['DocumentationComment'],
            'package-documentation-comment': names['PackageDocumentationComment'],
            'single-line-comment': names['SinglelineComment'], 'multi-line-comment': names['MultilineComment'],
        }

    def expected(self, token):
        """The type and value that the compiler's lexer gives this token (value None: not compared)."""
        text = token.text
        kind = token.kind
        if kind == 'keyword':
            return self.keywords[text], text
        value = {
            'triangle-operator': lambda: strip_fe0f(text),
            'identifier': lambda: strip_fe0f(text),
            'no-value': lambda: strip_fe0f(text),
            'else': lambda: strip_fe0f(text),
            'else-if': lambda: strip_fe0f(text)[:-1],
            'variable': lambda: text,
            'integer-literal': lambda: text.replace(',', ''),
            'float-literal': lambda: text.replace(',', ''),
            'string-literal': lambda: unescape(text[1:-1]),
            'interpolation-begin': lambda: unescape(text[1:-1]),
            'interpolation-middle': lambda: unescape(text[1:-1]),
            'interpolation-end': lambda: unescape(text[1:-1]),
            'decorator': lambda: text[1:],
            'symbol-literal': lambda: text[1:],
            'documentation-comment': lambda: text[1:-1],
            'package-documentation-comment': lambda: text[1:-1],
        }.get(kind)
        return self.types[kind], value() if value else None

    def compare(self, mine, theirs):
        for index, (token, (start, kind, value)) in enumerate(zip(mine, theirs)):
            expected_kind, expected_value = self.expected(token)
            if token.start != start or expected_kind != kind or (expected_value is not None and
                                                                  expected_value != value):
                return 'token {}: grammar {} {} {!r}, compiler {} {} {!r}'.format(
                    index, token.start, expected_kind, expected_value, start, kind, value)
        if len(mine) != len(theirs):
            return 'grammar has {} tokens, compiler {}'.format(len(mine), len(theirs))
        return None


def describe_failure(tokens, index):
    if index >= len(tokens):
        return 'at the end of the document'
    around = ' '.join(t.text for t in tokens[max(0, index - 4):index + 4])
    return 'near token {} ({!r}) in: {}'.format(index, tokens[index].text, around)


class Checker:
    def __init__(self, grammar, build):
        self.g = grammar
        self.lexer = Lexer(grammar)
        self.text = self.lexer.text
        self.build = build
        self.emojicodec = os.path.join(build, 'Compiler', 'emojicodec')
        self.comparison = TokenComparison()

    def grammar_verdict(self, source):
        """(accepted, reason) for a document according to the grammar."""
        try:
            tokens = self.lexer.lex(source)
        except LexError as error:
            return False, 'lexical error ' + str(error)
        failed = run_deep(parses, self.g, self.text, tokens)
        if failed is None:
            return True, None
        return False, 'syntax error ' + describe_failure(tokens, failed)

    def compiler_verdict(self, path, package=None):
        """(accepted, messages). accepted is None if an error is outside the scope of the grammar: such an error can
        make the parser skip code and report further errors that are not real (e.g. after a duplicate 🏁)."""
        command = [self.emojicodec, '--parse-only', '--json', '-S', self.build, path]
        if package:
            command[1:1] = ['-p', package]
        completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        try:
            messages = [m['message'] for m in json.loads(completed.stdout.decode('utf-8')) if m['type'] == 'error']
        except ValueError:
            output = (completed.stdout + completed.stderr).decode('utf-8', 'replace').strip()
            return 'crash', ['compiler crashed: ' + output[-300:]]
        if any(pattern in m for m in messages for pattern in CONTEXT_ERRORS):
            return None, messages
        return not messages, messages

    def check_corpus(self, report, paths):
        report.section('Corpus: tokens and syntax of files the compiler builds')
        token_failures = syntax_failures = 0
        for path in paths:
            source = open(path, encoding='utf-8').read()
            name = os.path.relpath(path, ROOT)
            theirs, error = compiler_tokens(self.emojicodec, path)
            try:
                mine = self.lexer.lex(source, keep_comments=True)
            except LexError as e:
                if error is None:
                    report.fail('{}: grammar lexical error {}'.format(name, e))
                    token_failures += 1
                continue
            if error is not None:
                report.fail('{}: compiler lexical error {}, grammar none'.format(name, error))
                token_failures += 1
                continue
            difference = self.comparison.compare(mine, theirs)
            if difference:
                report.fail('{}: tokens differ, {}'.format(name, difference))
                token_failures += 1
            tokens = [t for t in mine if t.kind not in ('single-line-comment', 'multi-line-comment')]
            failed = run_deep(parses, self.g, self.text, tokens)
            if failed is not None:
                report.fail('{}: not accepted by the grammar, {}'.format(name, describe_failure(tokens, failed)))
                syntax_failures += 1
        if not token_failures:
            report.ok('{} files tokenized identically by the grammar and the compiler'.format(len(paths)))
        if not syntax_failures:
            report.ok('{} files accepted by the grammar'.format(len(paths)))

    def compare_verdicts(self, report, cases, what):
        """cases: (label, path, source, package). Runs the compiler in parallel."""
        agreed = skipped = crashed = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            results = pool.map(lambda case: self.compiler_verdict(case[1], case[3]), cases)
            for (label, path, source, package), (theirs, messages) in zip(cases, results):
                if theirs is None:
                    skipped += 1
                    continue
                if theirs == 'crash':
                    crashed += 1
                    report.warn('{}: {}'.format(label, messages[0]))
                    continue
                mine, reason = self.grammar_verdict(source)
                if mine == theirs:
                    agreed += 1
                elif theirs:
                    report.fail('{}: accepted by the compiler, rejected by the grammar ({})'.format(label, reason))
                else:
                    report.fail('{}: rejected by the compiler ({}), accepted by the grammar'.format(
                        label, '; '.join(messages[:2])))
        report.ok('{} {}: {} agree, {} skipped (errors outside the grammar), {} crashed the compiler'.format(
            len(cases), what, agreed, skipped, crashed))

    def compare_tokens(self, report, cases):
        """cases: (label, path, source). The grammar and the compiler must produce the same tokens or both fail."""
        agreed = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            results = pool.map(lambda case: compiler_tokens(self.emojicodec, case[1]), cases)
            for (label, path, source), (theirs, error) in zip(cases, results):
                try:
                    mine = self.lexer.lex(source, keep_comments=True)
                    mine_error = None
                except LexError as e:
                    mine_error = str(e)
                if mine_error or error:
                    if mine_error and error:
                        agreed += 1
                    else:
                        report.fail('{}: lexical error only in the {}: {}'.format(
                            label, 'grammar' if mine_error else 'compiler', mine_error or error))
                    continue
                difference = self.comparison.compare(mine, theirs)
                if difference:
                    report.fail('{}: tokens differ, {}'.format(label, difference))
                else:
                    agreed += 1
        report.ok('{} character mutants: {} tokenized identically'.format(len(cases), agreed))

    def check_generated(self, report, corpus, count, seed):
        report.section('Generation: the compiler accepts documents generated from the grammar ({}, seed {})'.format(
            count, seed))
        tokens = []
        for path in corpus:
            try:
                tokens.extend(self.lexer.lex(open(path, encoding='utf-8').read()))
            except LexError:
                pass
        generator = Generator(self.g, self.lexer, tokens, random.Random(seed))
        directory = tempfile.mkdtemp(prefix='grammar_generate_')
        with open(os.path.join(directory, 'included.🍇'), 'w', encoding='utf-8') as f:
            f.write('💭 Included by generated documents.\n')
        cases = []
        used = []
        attempts = 0
        while len(cases) < count and attempts < count * 20:
            attempts += 1
            try:
                texts, alternatives = run_deep(generator.document)
            except Retry:
                continue
            source = ' '.join(texts) + '\n'
            try:
                if [t.text for t in self.lexer.lex(source)] != texts:
                    continue
            except LexError:
                continue
            path = os.path.join(directory, '{}.emojic'.format(len(cases)))
            with open(path, 'w', encoding='utf-8') as f:
                f.write(source)
            cases.append(('generated #{}: {}'.format(len(cases), source.strip()[:300]), path, source, None))
            used.append(alternatives)

        accepted = skipped = crashed = 0
        exercised = set()
        with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            results = pool.map(lambda case: self.compiler_verdict(case[1]), cases)
            for (label, path, source, _), alternatives, (theirs, messages) in zip(cases, used, results):
                mine, reason = self.grammar_verdict(source)
                if not mine:
                    report.fail('{}: generated but rejected by the grammar ({})'.format(label, reason))
                elif theirs is None:
                    skipped += 1
                elif theirs == 'crash':
                    crashed += 1
                    report.warn('{}: {}'.format(label, messages[0]))
                elif theirs:
                    accepted += 1
                    exercised |= alternatives
                else:
                    report.fail('{}: rejected by the compiler ({})'.format(label, '; '.join(messages[:2])))
        shutil.rmtree(directory)
        report.ok('{} documents: {} accepted by the compiler, {} skipped (errors outside the grammar), {} crashed '
                  'the compiler'.format(len(cases), accepted, skipped, crashed))
        total = generator.alternatives()
        report.ok('{} of {} alternatives in the syntactic grammar exercised by accepted documents'.format(
            len(exercised & total), len(total)))
        for rule, alt, index in sorted(total - exercised, key=lambda a: (self.g.order.index(a[0]), a[2])):
            expression = next(e for e in walk(self.g.rules[rule]) if id(e) == alt)
            print('       not exercised: {} ::= ... {} ...'.format(rule, render(expression[1][index])))

    def check_rejects(self, report, paths):
        report.section('Reject tests: grammar and compiler parser agree')
        cases = [(os.path.relpath(p, ROOT), p, open(p, encoding='utf-8').read(), None) for p in paths]
        self.compare_verdicts(report, cases, 'reject tests')

    def check_mutations(self, report, paths, count, seed):
        report.section('Mutations: grammar and compiler parser agree ({} per file, seed {})'.format(count, seed))
        rng = random.Random(seed)
        documents = []
        sources = {}
        for path in paths:
            sources[path] = open(path, encoding='utf-8').read()
            try:
                documents.append((path, self.lexer.lex(sources[path])))
            except LexError:
                pass
        pool = sorted({t.text for _, tokens in documents for t in tokens})
        directory = tempfile.mkdtemp(prefix='grammar_check_')
        cases = []
        lexical_cases = []
        for path, tokens in documents:
            if len(tokens) < 2:
                continue
            package = os.path.basename(os.path.dirname(path))
            if os.path.dirname(path) != os.path.join(ROOT, package) or package not in PACKAGES:
                package = None
            for n in range(count):
                texts = [t.text for t in tokens]
                k = rng.randrange(len(texts))
                operation = rng.choice(['delete', 'duplicate', 'swap', 'replace', 'insert character',
                                        'delete character', 'end without newline'])
                original = sources[path]
                c = rng.randrange(len(original))
                if operation == 'insert character':
                    character = rng.choice(LEXICAL_CHARACTERS + [rng.choice(original)])
                    source = original[:c] + character + original[c:]
                    k = '{} {!r}'.format(c, character)
                elif operation == 'delete character':
                    source = original[:c] + original[c + 1:]
                    k = '{} {!r}'.format(c, original[c])
                elif operation == 'end without newline':
                    source = original.rstrip(WHITESPACE)
                    k = len(source)
                elif operation == 'delete':
                    del texts[k]
                elif operation == 'duplicate':
                    texts.insert(k, texts[k])
                elif operation == 'swap' and k + 1 < len(texts):
                    texts[k], texts[k + 1] = texts[k + 1], texts[k]
                else:
                    operation = 'replace'
                    texts[k] = rng.choice(pool)
                if 'character' not in operation and 'newline' not in operation:
                    source = ' '.join(texts) + '\n'
                label = '{} #{} ({} at {})'.format(os.path.relpath(path, ROOT), n, operation, k)
                mutant_directory = os.path.join(directory, str(len(cases)))
                os.mkdir(mutant_directory)
                if package:
                    for other in glob.glob(os.path.join(ROOT, package, '*.🍇')):
                        shutil.copy(other, mutant_directory)
                    mutant = os.path.join(mutant_directory, os.path.basename(path))
                    main_file = os.path.join(mutant_directory, package + '.🍇')
                else:
                    mutant = main_file = os.path.join(mutant_directory, os.path.basename(path))
                with open(mutant, 'w', encoding='utf-8') as f:
                    f.write(source)
                cases.append((label, main_file, source, package))
                if 'character' in operation or 'newline' in operation:
                    lexical_cases.append((label, mutant, source))
        self.compare_tokens(report, lexical_cases)
        self.compare_verdicts(report, cases, 'mutants')
        shutil.rmtree(directory)


class Retry(Exception):
    pass


class Generator:
    """Generates random documents from the syntactic grammar."""

    DEPTH = 45
    # Once a document has this many tokens, the generator takes the shortest way to finish it.
    BUDGET = 200
    # How often a repetition (* or +) is repeated. Repetitions that start with a terminal, like ( '➕' operand )*,
    # are rare: expressions nest through ten such levels of binary operators.
    REPETITIONS = [0] * 5 + [1] * 3 + [2] * 2
    OPERATOR_REPETITIONS = [0] * 9 + [1]
    SYNTHETIC = {
        # Identifiers that are unlikely to collide with names in the s package.
        'identifier': [chr(c) for c in range(0x1F980, 0x1F9A3)] + [chr(c) for c in range(0x1F950, 0x1F96C)],
        # files and json are packages, so that 📦 can succeed.
        'variable': ['a', 'b', 'value', 'x2', 'files', 'json'],
        'documentation-comment': ['📗 Documentation. 📗'],
        'package-documentation-comment': ['📘 Package. 📘'],
        # INCLUDED is written next to each generated document, so that 📜 can succeed.
        'string-literal': ['🔤text🔤', '🔤included.🍇🔤'],
        'non-empty-string-literal': ['🔤text🔤'],
        'interpolation-begin': ['🔤a🧲'],
        'interpolation-middle': ['🧲b🧲'],
        'interpolation-end': ['🧲c🔤'],
        'integer-literal': ['2'],
        'float-literal': ['2.5'],
        'no-value': ['🤷'],
        'else': ['🙅'],
        'else-if': ['🙅↪'],
    }

    def __init__(self, grammar, lexer, corpus_tokens, rng):
        self.g = grammar
        self.lexer = lexer
        self.rng = rng
        self.usage = {}
        self.pools = {}
        referenced = {e[1] for name in grammar.order if name not in grammar.lexical
                      for e in walk(grammar.rules[name]) if e[0] == 'ref' and e[1] in grammar.lexical}
        texts = sorted({t.text for t in corpus_tokens})
        for name in referenced:
            pool = [t for t in texts if name == 'token' or lexer.text.full(name, t)]
            if name in self.SYNTHETIC:
                pool = self.SYNTHETIC[name] * 4 + pool
            if not pool:
                raise GrammarError('No example token for {} in the corpus.'.format(name))
            self.pools[name] = pool
        self.height = self._heights()

    def _heights(self):
        height = {name: None for name in self.g.rules if name not in self.g.lexical}

        def of(e):
            kind = e[0]
            if kind == 'lit' or (kind == 'ref' and e[1] in self.g.lexical):
                return 0
            if kind == 'ref':
                return None if height[e[1]] is None else height[e[1]] + 1
            if kind in ('opt', 'star'):
                return 0
            if kind in ('plus', 'diff'):
                return of(e[1])
            if kind == 'seq':
                parts = [of(x) for x in e[1]]
                return None if None in parts else max(parts)
            if kind == 'alt':
                parts = [of(x) for x in e[1] if of(x) is not None]
                return min(parts) if parts else None
            return 0

        changed = True
        while changed:
            changed = False
            for name in height:
                h = of(self.g.rules[name])
                if h is not None and (height[name] is None or h < height[name]):
                    height[name] = h
                    changed = True
        self.of = of
        return height

    def document(self):
        """A list of token texts, and the (rule, alternative) pairs used."""
        self.used = set()
        self.size = 0
        return self.gen(('ref', 'document'), 0, 'document'), self.used

    def gen(self, e, depth, rule):
        kind = e[0]
        if kind == 'lit':
            self.size += 1
            return [e[1]]
        if kind == 'ref':
            if e[1] in self.g.lexical:
                self.size += 1
                return [self.rng.choice(self.pools[e[1]])]
            return self.gen(self.g.rules[e[1]], depth + 1, e[1])
        if kind == 'seq':
            return [t for x in e[1] for t in self.gen(x, depth, rule)]
        if kind == 'alt':
            choices = [(i, x) for i, x in enumerate(e[1]) if self.of(x) is not None and depth + self.of(x) <= self.DEPTH]
            if not choices or self.size > self.BUDGET:
                lowest = min(self.of(x) for x in e[1] if self.of(x) is not None)
                choices = [(i, x) for i, x in enumerate(e[1]) if self.of(x) == lowest]
            least = min(self.usage.get((rule, id(e), i), 0) for i, _ in choices)
            i, x = self.rng.choice([(i, x) for i, x in choices if self.usage.get((rule, id(e), i), 0) == least])
            self.usage[(rule, id(e), i)] = self.usage.get((rule, id(e), i), 0) + 1
            self.used.add((rule, id(e), i))
            return self.gen(x, depth, rule)
        if kind in ('opt', 'star', 'plus'):
            if depth + (self.of(e[1]) or 0) > self.DEPTH or self.size > self.BUDGET:
                count = 1 if kind == 'plus' else 0
            elif kind == 'opt':
                count = self.rng.randrange(2)
            else:
                body = e[1][1][0] if e[1][0] == 'seq' else None
                operator_like = body is not None and (body[0] == 'lit' or (
                    body[0] == 'alt' and all(x[0] == 'lit' for x in body[1])))
                count = (1 if kind == 'plus' else 0) + self.rng.choice(
                    self.OPERATOR_REPETITIONS if operator_like else self.REPETITIONS)
            return [t for _ in range(count) for t in self.gen(e[1], depth, rule)]
        if kind == 'diff':
            for _ in range(20):
                texts = self.gen(e[1], depth, rule)
                tokens = self.lexer.lex(' '.join(texts) + '\n')
                matcher = Matcher(self.g, tokens, True, self.lexer.text)
                if not contains(matcher.ends(e[2], 0), len(tokens)):
                    return texts
            raise Retry()
        raise GrammarError('Cannot generate ' + kind)

    def alternatives(self):
        """The alternatives the generator can choose: not those on the right-hand side of an exclusion."""
        excluded = {id(x) for name in self.g.order for e in walk(self.g.rules[name]) if e[0] == 'diff'
                    for x in walk(e[2])}
        return {(name, id(e), i) for name in self.g.order if name not in self.g.lexical
                for e in walk(self.g.rules[name]) if e[0] == 'alt' and id(e) not in excluded
                for i in range(len(e[1]))}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', help='build directory containing Compiler/emojicodec')
    parser.add_argument('--grammar', default=GRAMMAR_PATH, help='grammar to check (default: docs/grammar.ebnf)')
    parser.add_argument('--mutations', type=int, default=0, help='variants to test per corpus file')
    parser.add_argument('--generate', type=int, default=0, help='documents to generate from the grammar')
    parser.add_argument('--seed', type=int, default=1, help='random seed for --mutations and --generate')
    parser.add_argument('files', nargs='*', help='check only these corpus files')
    args = parser.parse_args()

    sys.setrecursionlimit(1000000)
    threading.stack_size(512 * 1024 * 1024)

    report = Report()
    grammar = Grammar(args.grammar)
    check_well_formed(grammar, report)
    check_tables(grammar, report)
    lexer = Lexer(grammar)
    check_literals(grammar, lexer, report)

    if args.build:
        checker = Checker(grammar, os.path.abspath(args.build))
        if args.files:
            corpus = [os.path.abspath(f) for f in args.files]
        else:
            corpus = sorted(p for pattern in CORPUS_PATTERNS for p in glob.glob(os.path.join(ROOT, pattern)))
        checker.check_corpus(report, corpus)
        if not args.files:
            checker.check_rejects(report, sorted(glob.glob(os.path.join(ROOT, REJECT_PATTERN))))
        if args.mutations:
            checker.check_mutations(report, corpus, args.mutations, args.seed)
        if args.generate:
            checker.check_generated(report, corpus, args.generate, args.seed)
    else:
        print('\n(no --build given: skipping the checks that run the compiler)')

    print()
    if report.warnings:
        print('⚠️  {} warnings: the compiler crashed on documents it should have accepted or rejected'.format(
            report.warnings))
    if report.failures:
        print('❌ {} problems found'.format(report.failures))
        return 1
    print('✅ The grammar matches the compiler.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
