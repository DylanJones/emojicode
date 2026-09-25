/**
 * Tree-sitter grammar for Emojicode, ported from docs/grammar.ebnf.
 *
 * The compiler is the authoritative definition of the syntax. This grammar is meant for editors: it accepts what
 * the compiler accepts (tools/grammar_check.py --tree-sitter checks this against the repository's code) and
 * produces a tree for highlighting, folding, indentation and text objects. It is more permissive than the compiler
 * in places, e.g. it does not check the order of attributes.
 */

// Regular expressions with \u{...} need the u flag in JavaScript. Tree-sitter only reads their source.
const re = source => new RegExp(source, 'u');

// The lexer's character sets, read from docs/grammar.ebnf so that they match the compiler exactly.
const EBNF = require('fs').readFileSync(require('path').join(__dirname, '..', '..', 'docs', 'grammar.ebnf'), 'utf8');

/// Returns the text of the rule @p name of the EBNF grammar.
function ebnfRule(name) {
  const start = EBNF.search(new RegExp('^' + name + ' +::=', 'm'));
  const next = EBNF.indexOf('\n', start) + 1;
  const end = next + EBNF.slice(next).search(/^[a-z-]+ +::=|^\/\*|^$/m);
  return EBNF.slice(start, end);
}

/// Returns the code point ranges of the sets like [#xA9#x2194-#x2199] in @p rule as [low, high] pairs.
function ranges(rule) {
  return [...rule.matchAll(/#x([0-9A-F]+)(?:-#x([0-9A-F]+))?/g)]
    .map(([, low, high]) => [parseInt(low, 16), parseInt(high || low, 16)]);
}

/// Returns a character class for @p ranges without the code points in @p excluded.
function characterClass(ranges, excluded = []) {
  const parts = [];
  for (let [low, high] of ranges) {
    for (const c of [...excluded].sort((a, b) => a - b)) {
      if (low <= c && c <= high) {
        if (low < c) parts.push([low, c - 1]);
        low = c + 1;
      }
    }
    if (low <= high) parts.push([low, high]);
  }
  const hex = c => '\\u{' + c.toString(16).toUpperCase() + '}';
  return '[' + parts.map(([low, high]) => low === high ? hex(low) : hex(low) + '-' + hex(high)).join('') + ']';
}

// Every code point that is a keyword or starts another kind of token is a token of its own, even if emoji are
// joined to it: 🐚‍🥞 is 🐚 followed by other tokens.
const NOT_IDENTIFIER_START = [...(ebnfRule('keyword') + ebnfRule('not-identifier-start')).matchAll(/'([^']+)'/g)]
  .map(([, text]) => text.codePointAt(0));
const EMOJI_RANGES = ranges(ebnfRule('emoji-char'));
const EMOJI_CHAR = characterClass(EMOJI_RANGES);
const IDENTIFIER_START_CHAR = characterClass(EMOJI_RANGES, NOT_IDENTIFIER_START);

// An emoji, or a flag of two regional indicators, optionally with a skin tone. U+FE0F is whitespace to the lexer and
// is not part of an identifier unless a joiner follows, so that ❗️ is the keyword ❗ followed by whitespace.
const EMOJI = '([\\u{1F1E6}-\\u{1F1FF}]{1,2}|' + EMOJI_CHAR + '[\\u{1F3FB}-\\u{1F3FF}]?)';
const IDENTIFIER_START = '([\\u{1F1E6}-\\u{1F1FF}]{1,2}|' + IDENTIFIER_START_CHAR + '[\\u{1F3FB}-\\u{1F3FF}]?)';
// Emoji joined with U+200D or 🔸, as the raw-identifier rule of docs/grammar.ebnf describes. The first emoji after a
// joiner is always the joined one, even if it is 🔸, and only the last joiner can be without one.
const JOINER = '\\u{FE0F}*[\\u{200D}\\u{1F538}][\\u{FE0F}\\u{200D}]*';
const JOINED = '(' + JOINER + EMOJI + ')*';
// An identifier cannot end with U+200D, so it can only end with a 🔸 without an emoji after it.
const IDENTIFIER = re(IDENTIFIER_START + JOINED + '(\\u{FE0F}*\\u{1F538}\\u{FE0F}*)?');
// The parser recognizes some emoji by the first code point of an identifier token, so 🍺🔸🐟 is the operator 🍺.
// These keywords also match the rest of such a token.
const IDENTIFIER_TAIL = '[\\u{1F3FB}-\\u{1F3FF}]?' + JOINED + '(\\u{FE0F}*\\u{1F538}\\u{FE0F}*)?';
const kw = text => alias(token(prec(2, seq(text, re(IDENTIFIER_TAIL)))), text);
// Unlike identifiers, 🤷 and 🙅 may end with any joiner.
const JOINED_OR_TRAILING = JOINED + '(' + JOINER + ')?';
// The whitespace rule of docs/grammar.ebnf. U+FE0F is whitespace to the lexer.
const WHITESPACE = '\\u{9}-\\u{D}\\u{20}\\u{85}\\u{A0}\\u{1680}\\u{2000}-\\u{200A}\\u{2028}\\u{2029}\\u{202F}\\u{205F}\\u{3000}\\u{FE0F}';
const NOT_VARIABLE = WHITESPACE + EMOJI_CHAR.slice(1, -1);

const PREC = {
  or: 1, and: 2, bitOr: 3, bitXor: 4, bitAnd: 5, equality: 6, comparison: 7, shift: 8, additive: 9,
  multiplicative: 10, prefix: 11, call: 12,
};

const BINARY = [
  ['or', ['👐']],
  ['and', ['🤝']],
  ['bitOr', ['💢']],
  ['bitXor', ['❌']],
  ['bitAnd', ['⭕']],
  ['equality', ['🙌', '😜']],
  // ◀ ▶ ◀🙌 ▶🙌, with any number of U+FE0F in between.
  ['comparison', [token(seq(/[◀▶]/, optional(seq(/\uFE0F*/, '🙌'))))]],
  ['shift', ['👈', '👉']],
  ['additive', ['➕', '➖']],
  ['multiplicative', ['✖', '➗', '🚮']],
];

module.exports = grammar({
  name: 'emojicode',

  extras: $ => [re('[' + WHITESPACE + ']'), $.comment, $.documentation_comment],

  word: $ => $.identifier,

  conflicts: $ => [
    [$.method],
    [$.initializer],
    [$._member_attribute, $.instance_variable],
    [$.error_handler, $.primary_variable],
    [$.parameter, $.primary_variable],
    [$.closure, $.parameter],
  ],

  rules: {
    source_file: $ => repeat($._document_statement),

    _document_statement: $ => choice(
      $.package_import,
      $.include,
      $.start_flag,
      $.link_hints,
      $._type_definition,
    ),

    package_import: $ => seq(kw('📦'), field('package', $.variable), field('namespace', $.identifier)),
    include: $ => seq(kw('📜'), field('path', $.string)),
    start_flag: $ => seq(kw('🏁'), optional($.return_type), field('body', $.block)),
    link_hints: $ => seq(kw('🔗'), repeat1($.string), kw('🔗')),

    // Comments

    comment: _ => token(choice(
      seq('💭🔜', /([^🔚]|🔚+[^🔚💭])*/, /🔚+💭/),
      // Ends at a line break, which is also U+2028 or U+2029.
      /💭([^🔜\n\u2028\u2029][^\n\u2028\u2029]*)?/,
    )),
    documentation_comment: _ => token(choice(/📗[^📗]*📗/, /📘[^📘]*📘/)),

    // Type definitions

    _type_definition: $ => choice($.class_definition, $.value_type_definition, $.enum_definition,
                                  $.protocol_definition),

    _type_attribute: $ => choice(kw('🌍'), kw('🔏'), $.decorator, seq(kw('📻'), optional($.string))),

    class_definition: $ => seq(
      repeat($._type_attribute), '🐇', field('name', $.type_identifier),
      optional($.generic_parameters), optional(field('superclass', $._type_not_callable)),
      field('body', $.type_body),
    ),
    value_type_definition: $ => seq(
      repeat($._type_attribute), '🕊', field('name', $.type_identifier),
      optional($.generic_parameters), field('body', $.type_body),
    ),
    enum_definition: $ => seq(repeat($._type_attribute), '🔘', field('name', $.type_identifier),
                              field('body', $.type_body)),
    protocol_definition: $ => seq(repeat($._type_attribute), '🐊', field('name', $.type_identifier),
                                  optional($.generic_parameters), field('body', $.type_body)),

    generic_parameters: $ => seq('🐚', repeat($.generic_parameter), kw('🍆')),
    generic_parameter: $ => seq(optional('☣'), field('name', $.variable), field('constraint', $._type)),

    type_body: $ => seq('🍇', repeat($._member), '🍉'),

    _member: $ => choice(
      $.instance_variable,
      $.method,
      $.initializer,
      $.deinitializer,
      $.protocol_conformance,
    ),

    access_level: _ => choice(kw('🔓'), kw('🔒'), kw('🔐')),
    _member_attribute: $ => choice(kw('🥯'), kw('⚠'), kw('🔏'), kw('✒'), '🐇', '☣', '🖍', kw('🔑'), $.decorator, $.access_level),

    instance_variable: $ => seq('🖍', optional($.access_level), '🆕', field('name', $.variable),
                                field('type', $._type), optional(seq('⬅', field('value', $._expression)))),

    protocol_conformance: $ => seq(optional($.access_level), '🐊', $._type),
    deinitializer: $ => prec.right(seq(optional($.access_level), kw('♻'), optional(field('body', $._function_body)))),

    // A method without a body is a protocol method or a method in an interface file.
    // Without a body, a decorator after a method could be a parameter's or the next member's, so both are tried.
    method: $ => prec.dynamic(0, seq(
      repeat($._member_attribute),
      choice(
        seq(field('mood', choice('❗', '❓', '➡')), field('name', $.identifier)),
        field('name', $.operator),
      ),
      optional($.generic_parameters),
      repeat($.parameter),
      optional($.return_type),
      optional($.error_type),
      optional(field('body', $._function_body)),
    )),

    // Enum values are initializers without parameters and body. As for methods, a decorator after one could be a
    // parameter's or the next member's.
    initializer: $ => prec.dynamic(0, seq(
      repeat($._member_attribute),
      '🆕',
      optional(seq('▶', field('name', $.identifier))),
      optional($.generic_parameters),
      repeat($.parameter),
      optional($.error_type),
      optional(field('body', $._function_body)),
    )),

    parameter: $ => seq(optional(kw('🍼')), optional($.decorator), field('name', $.variable), field('type', $._type)),
    return_type: $ => seq('➡', $._type),
    error_type: $ => seq(kw('🚧'), $._type),

    _function_body: $ => choice($.block, $.external_body),
    external_body: $ => prec.right(seq(kw('📻'), $.string, optional($.block))),

    // Types

    _type: $ => choice($._type_not_callable, $.callable_type),
    _type_not_callable: $ => choice(
      $.type_value_type,
      $.no_return_type,
      $.something_type,
      $.optional_type,
      $.reference_type,
      $._type_main,
    ),
    type_value_type: $ => seq(choice('🐇', '🕊', '🔘', '🐊'), $._type),
    no_return_type: _ => kw('◼'),
    something_type: _ => seq(optional(kw('✴')), kw('⚪')),
    optional_type: $ => seq(kw('🍬'), optional(kw('✴')), $._type_main_or_callable),
    reference_type: $ => seq(kw('✴'), $._type_main_or_callable),
    _type_main_or_callable: $ => choice($._type_main, $.callable_type),
    _type_main: $ => choice(
      alias($.variable, $.type_variable),
      $.multi_protocol,
      $.someobject_type,
      $._named_type,
    ),
    _named_type: $ => prec.right(seq($.type_identifier, optional($.generic_arguments))),
    someobject_type: _ => kw('🔵'),
    callable_type: $ => seq('🍇', optional($.decorator), repeat($._type),
                            optional(seq('➡', $._type, optional(seq(kw('🚧'), $._type)))), '🍉'),
    // A type in a multiprotocol cannot start with 🍱, which ends it.
    multi_protocol: $ => seq(kw('🍱'), repeat($._type_in_multi_protocol), kw('🍱')),
    _type_in_multi_protocol: $ => choice(
      $.type_value_type,
      $.no_return_type,
      $.something_type,
      $.optional_type,
      $.reference_type,
      $.callable_type,
      alias($.variable, $.type_variable),
      $.someobject_type,
      $._named_type,
    ),
    type_identifier: $ => seq(optional(seq(kw('🔶'), field('namespace', $._type_name))), field('name', $._type_name)),
    // 🔂 is a keyword, but also a valid type name.
    _type_name: $ => choice($.identifier, alias('🔂', $.identifier)),
    generic_arguments: $ => seq('🐚', repeat($._type), kw('🍆')),

    _type_expression: $ => choice(
      seq(kw('⬛'), $._prefix_expression),
      kw('⚫'),
      $.this,
      $._type,
    ),

    // Statements

    block: $ => seq('🍇', repeat($._statement), '🍉'),

    _statement: $ => choice(
      $.variable_declaration,
      $.if_statement,
      $.error_handler,
      $.repeat_while,
      $.for_in,
      $.unsafe_block,
      $.raise,
      $.return,
      $.operator_assignment,
      $.expression_statement,
    ),

    variable_declaration: $ => seq('🖍', '🆕', field('name', $.variable), field('type', $._type)),
    if_statement: $ => prec.right(seq(
      '↪', $._condition, optional($.branch_hint), field('consequence', $.block),
      repeat($.else_if), optional($.else),
    )),
    else_if: $ => seq(alias($._else_if, '🙅↪'), $._condition, optional($.branch_hint), field('consequence', $.block)),
    else: $ => seq(alias($._else, '🙅'), field('body', $.block)),
    // Like 🤷, 🙅 may be joined with other emoji, e.g. 🙅‍♂️.
    _else: _ => token(prec(1, seq('🙅', re('[\\u{1F3FB}-\\u{1F3FF}]?' + JOINED_OR_TRAILING)))),
    _else_if: _ => token(prec(1, seq('🙅', re('[\\u{1F3FB}-\\u{1F3FF}]?' + JOINED + '\\u{FE0F}?↪')))),
    branch_hint: $ => repeat1($.decorator),
    _condition: $ => seq($._expression, optional(seq('➡', field('binding', $.variable)))),

    error_handler: $ => seq('🆗', optional(field('binding', $.variable)), $._expression, field('body', $.block),
                            alias($._else, '🙅'), field('error', $.variable), field('error_body', $.block)),
    repeat_while: $ => seq('🔁', $._condition, field('body', $.block)),
    for_in: $ => seq('🔂', field('element', $.variable), field('collection', $._expression), field('body', $.block)),
    unsafe_block: $ => seq('☣', field('body', $.block)),
    raise: $ => seq('🚨', $._expression),
    return: $ => prec.right(seq('↩', optional(choice('↩', $._expression)))),
    operator_assignment: $ => seq(field('name', $.variable), '⬅', $.operator, $._expression),
    expression_statement: $ => prec.right(seq($._expression, optional(seq('➡', $._assignment_target)))),
    _assignment_target: $ => choice(
      seq('🖍', '🆕', field('declares', $.variable)),
      seq('🖍', field('assigns', $.variable)),
      field('declares', $.variable),
      alias($.assignment_call, $.method_call),
    ),
    assignment_call: $ => seq(field('name', $.identifier), optional($.generic_arguments),
                              repeat($._expression), $._mood),

    // Expressions

    _expression: $ => choice($.binary_expression, $._prefix_expression),

    binary_expression: $ => choice(...BINARY.map(([precedence, operators]) =>
      prec.left(PREC[precedence], seq(
        field('left', $._expression),
        field('operator', alias(operators.length === 1 ? operators[0] : choice(...operators), $.operator)),
        field('right', $._expression),
      )),
    )),

    _prefix_expression: $ => choice(
      $.unwrap,
      $.reraise,
      $.cast,
      $.callable_call,
      $._primary,
    ),
    unwrap: $ => prec(PREC.prefix, seq(kw('🍺'), $._prefix_expression)),
    reraise: $ => prec(PREC.prefix, seq(kw('🔺'), $._prefix_expression)),
    cast: $ => prec(PREC.prefix, seq(kw('🔲'), $._prefix_expression, $._type_expression)),
    callable_call: $ => seq('⁉', $._prefix_expression, repeat($._expression), $._mood),

    _primary: $ => choice(
      $.number,
      $.boolean,
      $.no_value,
      $.string,
      $.collection_literal,
      $.primary_variable,
      $.this,
      $.group,
      $.closure,
      $.instantiation,
      $.method_call,
      $.super_call,
      $.type_value,
      $.size_of,
      $.is_only_reference,
      $.selection,
    ),

    primary_variable: $ => $.variable,
    this: _ => '👇',
    group: $ => seq('🤜', $._expression, '🤛'),
    type_value: $ => seq(choice('🐇', '🕊', '🔘', '🐊'), $._type),
    size_of: $ => seq(kw('⚖'), $._type),
    is_only_reference: $ => seq(kw('🏮'), $.variable),
    selection: $ => seq('📣', $._expression, $._type_expression),

    closure: $ => seq(
      '🍇', repeat($.decorator), repeat($.parameter), optional($.return_type), optional($.error_type),
      repeat($._statement), '🍉',
    ),

    instantiation: $ => seq('🆕', field('type', $._type_expression),
                            optional(seq('▶', field('name', $.identifier))), optional($.generic_arguments),
                            repeat($._expression), $._mood),

    // The callee is the first argument.
    method_call: $ => prec(PREC.call, seq(field('name', $.identifier), optional($.generic_arguments),
                                          repeat($._expression), $._mood)),

    super_call: $ => seq('⤴', choice('🆕', field('name', $.identifier), $.operator), optional($.generic_arguments),
                         repeat($._expression), $._mood),

    _mood: $ => alias(choice('❗', '❓'), $.mood),

    collection_literal: $ => seq('🍿', repeat(choice($._expression, '➡')), kw('🍆')),

    // Tokens

    identifier: _ => IDENTIFIER,
    variable: _ => re('[^' + NOT_VARIABLE + '0-9+\\-][^' + NOT_VARIABLE + ']*'),
    decorator: _ => token(seq('🎍', re(EMOJI + '\\u{FE0F}?'))),
    operator: _ => choice('➕', '➖', '➗', '✖', '🚮', '👈', '👉', '⭕', '💢', '❌', '🙌', '😜', '🤝', '👐',
                          token(seq(/[◀▶]/, optional(seq(/\uFE0F*/, '🙌'))))),
    number: _ => token(choice(
      /0,*[xX][0-9a-fA-F,]*(\.[0-9]+)?/,
      /[0-9][0-9,]*(\.[0-9]+)?/,
      // A sign may be followed directly by the decimal point: -.5
      /[+\-],*([0-9][0-9,]*(\.[0-9]+)?|\.[0-9]+)/,
    )),
    boolean: _ => choice('👍', '👎'),
    no_value: _ => token(prec(1, seq('🤷', re('[\\u{1F3FB}-\\u{1F3FF}]?' + JOINED_OR_TRAILING)))),

    string: $ => seq(
      '🔤',
      repeat(choice($.string_content, $.escape_sequence, $.interpolation)),
      '🔤',
    ),
    string_content: _ => token.immediate(prec(1, /[^🔤🧲❌]+/)),
    escape_sequence: _ => token.immediate(/❌[\s\S]/),
    interpolation: $ => seq('🧲', $._expression, '🧲'),
  },
});
