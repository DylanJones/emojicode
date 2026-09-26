// Finds the section of the Emojicode documentation that explains a token, for the learner docs hover. This module
// does not depend on VS Code so that it can be tested with Node alone.
//
// The lexer follows Compiler/Lex/Lexer.cpp: strings, interpolations, comments and documentation comments are told
// apart first, so that an emoji in a string is explained as part of the string. The code tokens are then classified
// by their neighbours, e.g. whether ❗️ declares a method or calls one, which a keyword alone does not tell.

/** A section of the documentation website. */
export interface Section {
    /** A sentence for learners about the token. */
    summary: string;
    /** The heading of the section. */
    title: string;
    /** The path of the section relative to the root of the website, with its anchor. */
    path: string;
}

type PieceKind = 'emoji' | 'word' | 'number' | 'string' | 'escape' | 'interpolation' | 'comment' | 'documentation'
    | 'packageDocumentation' | 'symbol';

/** A token of code, or a part of a string or comment. Offsets are in UTF-16 code units, like JavaScript strings. */
export interface Piece {
    kind: PieceKind;
    /** The text without U+FE0F, which the lexer ignores. */
    text: string;
    start: number;
    end: number;
    /** Whether the piece is a 🔤 that starts a string, as a piece of a string can also start at the 🔤 that ends it. */
    opensString?: boolean;
}

interface Token extends Piece {
    line: number;
    /** The index of the matching bracket, e.g. the 🍉 of a 🍇. */
    partner?: number;
    /** The index of the innermost open 🍇, 🐚, 🍿 or 🤜 around this token. */
    enclosing?: number;
}

const ref = (page: string, anchor: string) => `docs/reference/${page}.html#${anchor}`;
const section = (summary: string, title: string, path: string): Section => ({ summary, title, path });

const S = {
    string: section('Text between 🔤 and 🔤 is a string. Emoji in it are just characters and do nothing.',
                    '🔤 String Literals', ref('literals', '-string-literals')),
    escape: section('❌ escapes the next character in a string: ❌n is a line break, ❌🔤 a 🔤 and ❌❌ a ❌.',
                    '🔤 String Literals', ref('literals', '-string-literals')),
    interpolation: section('🧲 marks code in a string: the value of the code between two 🧲 is inserted into the string.',
                           '🧲 Interpolation in String Literals', ref('literals', '-interpolation-in-string-literals')),
    comment: section('💭 starts a comment that lasts to the end of the line. 💭🔜 and 🔚💭 enclose a comment of many lines.',
                     'Comments', ref('basics', 'comments')),
    documentation: section('📗 encloses the documentation of the type, method or initializer that follows.',
                           'Documentation Comments', ref('documentation', 'documentation-comments')),
    packageDocumentation: section('📘 encloses the documentation of the whole package.', 'Documentation Comments',
                                  ref('documentation', 'documentation-comments')),
    symbol: section('🔟 followed by a character is a symbol literal: that single character.', 'Literals',
                    'docs/reference/literals.html'),
    number: section('A number. Numbers with a decimal point are 💯, all others are 🔢.', 'Numeric Literals',
                    ref('literals', 'numeric-literals')),
    variable: section('A variable: a name that stands for a value.', 'Variables and Assignment',
                      'docs/reference/variables.html'),

    start: section('🏁 🍇 … 🍉 is where the program starts running.', 'The 🏁 Block', ref('basics', 'the-block')),
    block: section('🍇 and 🍉 enclose a block of code, like braces in other languages.', '🍇🍉 Code Block',
                   ref('controlflow', '-code-block')),
    closure: section('This 🍇 … 🍉 is a closure: code that runs later, when the closure is called.', 'Closure',
                     ref('callables', 'closure')),
    callableType: section('🍇 … 🍉 in a type is a callable type, the type of closures.', 'Callables',
                          ref('callables', 'type')),
    typeBody: section('🍇 and 🍉 enclose the instance variables, methods and initializers of the type.',
                      'Defining a Class', ref('classes-valuetypes', 'defining-a-class')),
    methodBody: section('🍇 and 🍉 enclose the code of the method.', 'Methods', ref('classes-valuetypes', 'methods')),
    initializerBody: section('🍇 and 🍉 enclose the code of the initializer.', 'Initializers',
                             ref('classes-valuetypes', 'initializers')),
    deinitializer: section('♻️ declares a deinitializer, which runs when an object is released.', 'Deinitializers',
                           ref('memory', 'deinitializers')),

    if: section('↪️ runs the block if the condition is 👍.', '↪️ If', ref('controlflow', '-if')),
    else: section('🙅 runs the block if none of the conditions before it were 👍.', '🙅', ref('controlflow', '-')),
    elseIf: section('🙅↪️ checks another condition if none of the conditions before it were 👍.', '🙅↪️',
                    ref('controlflow', '-')),
    forIn: section('🔂 runs the block once for each value, e.g. each element of a list.', '🔂 For In',
                   ref('controlflow', '-for-in')),
    repeatWhile: section('🔁 runs the block again and again while the condition is 👍.', '🔁 Repeat While',
                         ref('controlflow', '-repeat-while')),
    branchSpeed: section('🎍🐌 and 🎍🏎 tell the compiler which branch is unlikely or likely to run.',
                         '🎍🐌 🎍🏎 Branch Speed', ref('controlflow', '-branch-speed')),
    return: section('↩️ returns a value from the method.', 'Returning Values', ref('classes-valuetypes', 'returning-values')),
    returnNothing: section('↩️↩️ returns from a method that has no return value.',
                           'Returning from Methods without Return Value',
                           ref('classes-valuetypes', 'returning-from-methods-without-return-value')),

    constant: section('➡️ stores the value on its left in the constant variable on its right.',
                      'Assigning a Constant Variable', ref('variables', 'assigning-a-constant-variable')),
    mutable: section('🖍🆕 declares a mutable variable, whose value can be changed with ➡️ 🖍.',
                     'Declaring and Assigning Mutable Variables',
                     ref('variables', 'declaring-and-assigning-mutable-variables')),
    assign: section('➡️ 🖍 stores the value on its left in the mutable variable on its right.',
                    'Changing the value of mutable variables',
                    ref('variables', 'changing-the-value-of-mutable-variables')),
    operatorAssign: section('⬅️ followed by an operator applies the operator to the variable, e.g. x ⬅️➕ 1 adds 1 to x.',
                            'Operator Assignment', ref('variables', 'operator-assignment')),
    instanceVariable: section('🖍🆕 in a type declares an instance variable, which each instance has its own value of.',
                              'Instance Variables', ref('classes-valuetypes', 'instance-variables')),
    defaultValue: section('⬅️ gives the instance variable the value it starts with.', 'Default Initialization Value',
                          ref('classes-valuetypes', 'default-initialization-value')),
    mutating: section('🖍 before a method of a value type lets it change the instance.', 'Mutability of Value Types',
                      ref('classes-valuetypes', 'mutability-of-value-types')),

    defineClass: section('🐇 defines a class, a type whose instances are shared by reference.', 'Defining a Class',
                         ref('classes-valuetypes', 'defining-a-class')),
    defineValueType: section('🕊 defines a value type, whose instances are copied when they are assigned.',
                             'Defining a Value Type', ref('classes-valuetypes', 'defining-a-value-type')),
    defineProtocol: section('🐊 defines a protocol: methods that the types conforming to it promise to have.',
                            'Protocols', ref('protocols', 'declaration')),
    conformance: section('🐊 in the body of a type declares that the type conforms to the protocol after it.',
                         'Conforming', ref('protocols', 'conforming')),
    defineEnum: section('🔘 defines an enumeration, a type with a fixed set of values.', 'Defining an Enumeration',
                        ref('enums', 'defining-an-enumeration')),
    typeMethod: section('🐇 before a method makes it a type method, which is called on the type, not on an instance.',
                        'Type Methods', ref('classes-valuetypes', 'type-methods')),
    declareMethod: section('❗️ declares a method. The emoji after it is the name of the method.', 'Methods',
                           ref('classes-valuetypes', 'methods')),
    declareQuestion: section('❓ declares a method that is called with ❓, which by convention answers a question.',
                             'Method Moods', ref('classes-valuetypes', 'method-moods')),
    call: section('❗️ ends a method call: 😀 🔤Hi🔤❗️ calls the method 😀 on 🔤Hi🔤.', 'Calling Methods',
                  ref('classes-valuetypes', 'calling-methods')),
    callQuestion: section('❓ ends a call of a method that was declared with ❓, which by convention answers a question.',
                          'Method Moods', ref('classes-valuetypes', 'method-moods')),
    callCallable: section('⁉️ calls a closure or other callable.', 'Calling a Callable',
                          ref('callables', 'calling-a-callable')),
    declareInitializer: section('🆕 declares an initializer, which sets up a new instance.', 'Initializers',
                                ref('classes-valuetypes', 'initializers')),
    instantiate: section('🆕 creates a new instance of the type after it.', 'Instantiation',
                         ref('classes-valuetypes', 'instantiation')),
    namedInitializer: section('▶️ names an initializer, so that a type can have several.', 'Named Initializer',
                              ref('classes-valuetypes', 'named-initializer')),
    returnType: section('➡️ in a declaration is followed by the type of value it returns.', 'Returning Values',
                        ref('classes-valuetypes', 'returning-values')),
    closureReturnType: section('➡️ at the start of a closure is followed by the type of value it returns.', 'Closure',
                               ref('callables', 'closure')),
    this: section('👇 is the instance that the method was called on, like this or self in other languages.',
                  'This Context', ref('classes-valuetypes', 'this-context')),
    super: section('⤴️ calls the method of the superclass.', 'Calling Super Methods',
                   ref('inheritance', 'calling-super-methods')),
    initParameter: section('🍼 before a parameter of an initializer copies it into the instance variable of that name.',
                           'Initializers', ref('classes-valuetypes', 'initializers')),

    exported: section('🌍 exports the type from its package, so that other packages can import it.', 'Exporting Types',
                      ref('packages', 'exporting-types')),
    final: section('🔏 prevents subclassing a class or overriding a method.', 'Final Classes',
                   ref('inheritance', 'final-classes')),
    override: section('✒️ overrides a method of the superclass.', 'Overriding Methods',
                      ref('inheritance', 'overriding-methods')),
    inline: section('🥯 asks the compiler to inline the method.', 'Inline', ref('classes-valuetypes', 'inline')),
    deprecated: section('⚠️ marks a method as deprecated.', 'Deprecation', ref('classes-valuetypes', 'deprecation')),
    required: section('🔑 requires subclasses to implement the initializer.', 'Required Initializers',
                      ref('inheritance', 'required-initializers')),
    access: section('🔓 public, 🔒 private and 🔐 protected set who can call the method.', 'Access Levels',
                    ref('classes-valuetypes', 'access-levels')),
    foreign: section('📻 means that the method is written in another language such as C.',
                     'Linking with Non-Emojicode Code', ref('packages', 'linking-with-non-emojicode-code')),
    unsafe: section('☣️ marks unsafe code, which can do things the compiler cannot check.', 'Unsafe Code',
                    ref('safety', 'unsafe-code')),

    true: section('👍 is true.', 'Booleans', ref('literals', 'booleans')),
    false: section('👎 is false.', 'Booleans', ref('literals', 'booleans')),
    list: section('🍿 … 🍆 is a list literal, or a dictionary literal if it contains ➡️.', '🍿 Collection Literal',
                  ref('literals', '-collection-literal')),
    dictionary: section('🍿 … 🍆 with ➡️ is a dictionary literal: each key on the left of ➡️ gets the value on its right.',
                        'Dictionary Literals', ref('literals', 'dictionary-literals')),
    generic: section('🐚 … 🍆 gives the generic arguments of a type, e.g. 🍨🐚🔢🍆 is a list of integers.',
                     'Generics', ref('generics', 'defining-a-generic-type')),
    grouping: section('🤜 and 🤛 group an expression, like parentheses.', 'Grouping', ref('operators', 'grouping')),
    operator: section('An operator, e.g. ➕ adds and ◀️ compares.', 'Operators', 'docs/reference/operators.html'),
    shortCircuit: section('🤝 is and, 👐 is or. The right side only runs if it is needed.',
                          'Short-Circuiting with 🤝 and 👐', ref('operators', 'short-circuiting-with-and-')),
    identity: section('😜 is 👍 if both sides are the same object.', 'Identity Check', ref('operators', 'identity-check')),

    optional: section('🍬 before a type makes it optional: the value can be 🤷‍♀️, no value.', 'Optionals',
                      'docs/reference/optionals.html'),
    noValue: section('🤷‍♀️ is no value, for optionals.', 'No Value', ref('optionals', 'no-value')),
    unwrap: section('🍺 gets the value of an optional or of an error-prone call, and stops the program if there is none.',
                    '🍺 Unwrapping', ref('optionals', '-unwrapping')),
    errorType: section('🚧 and an error type make a method error-prone: it returns a value or raises an error of that type.',
                       'Error-Proneness', ref('errors', 'error-proneness')),
    raise: section('🚨 raises an error, which ends the method.', 'Raising Errors', ref('errors', 'raising-errors')),
    reraise: section('🔺 passes the error of a call on to the caller.', '🔺 Reraising Errors',
                     ref('errors', '-reraising-errors')),
    handle: section('🆗 runs the block with the value of an error-prone call, and 🙅 the block after it with the error.',
                    '🆗 Handling Errors', ref('errors', '-handling-errors')),
    handleError: section('🙅 after 🆗 runs if the call raised an error.', '🆗 Handling Errors',
                         ref('errors', '-handling-errors')),

    namespace: section('🔶 gives the namespace of a type.', 'Namespaces', ref('types', 'namespaces')),
    cast: section('🔲 casts a value to a type, giving no value if it is not of that type.', '🔲 Type Casting',
                  ref('types', '-type-casting')),
    size: section('⚖️ gives the size of an instance of a type.', '⚖️ Size of Type Instance',
                  ref('types', '-size-of-type-instance')),
    something: section('⚪️ is any value.', '⚪ Something', ref('types', '-something')),
    someobject: section('🔵 is any object.', '🔵 Someobject', ref('types', '-someobject')),
    noReturn: section('◼️ means that there is no value.', '◼️ No Return', ref('types', '-no-return')),
    builtInType: section('A type built into the language.', 'Built-In Types', ref('types', 'built-in-types')),
    typeValue: section('⬛️ gives the type as a value, or refers to the type of a type value.', 'Types as Values',
                       'docs/reference/typevalues.html'),
    multiprotocol: section('🍱 … 🍱 is a type that conforms to several protocols.', 'Multiprotocols',
                           ref('protocols', 'multiprotocols')),
    reference: section('✴️ is a reference to a variable.', 'References', 'docs/reference/references.html'),
    shared: section('🏮 tells whether a value is shared.', 'Detecting Shared Values',
                    ref('memory', 'detecting-shared-values')),
    expectation: section('📣 evaluates the expression as the type after it.', 'Type Expectations',
                         ref('types', 'type-expectations')),

    package: section('📦 imports a package.', 'Importing Other Packages', ref('packages', 'importing-other-packages')),
    include: section('📜 includes the code of another file.', 'Including Other Source Code Files',
                     ref('basics', 'including-other-source-code-files')),
    link: section('🔗 names a library to link with.', 'Specifying Shared Libraries to Link',
                  ref('packages', 'specifying-shared-libraries-to-link')),
    decorator: section('🎍 and the emoji after it are a decorator.', 'Decorators', ref('syntax', 'decorators')),
    noDynamism: section('🎍🛢 disables generic dynamism for the type.', 'Disabling Generic Dynamism',
                        ref('generics', 'disabling-generic-dynamism')),
    escaping: section('🎍🥡 lets a closure or parameter escape: it can be kept after the call ends.',
                      'Borrowing and Escaping Use', ref('memory', 'borrowing-and-escaping-use')),
    cFunction: section('🎍🌊 makes a method a C function or a value type a C struct.', 'Calling C with 🎍🌊',
                       'docs/guides/c.html'),
};

export type SectionName = keyof typeof S;
export const sections: Record<SectionName, Section> = S;

const OPERATORS = new Set(['➕', '➖', '➗', '✖', '⭕', '💢', '❌', '👈', '👉', '🚮', '🙌', '◀', '▶']);
/** What can follow the 🍉 of a closure, as the expression goes on. */
const CONTINUATIONS = ['❗', '❓', '⁉', '➡', '🤛', '🍆'];
const MODIFIERS = new Set(['🌍', '🔏', '✒', '🥯', '⚠', '🔑', '☣', '🔓', '🔒', '🔐', '📻', '🍼']);
const DECORATORS: Record<string, SectionName> = {
    '🐌': 'branchSpeed', '🏎': 'branchSpeed', '🛢': 'noDynamism', '🥡': 'escaping', '🌊': 'cFunction',
};
/** Emoji that always mean the same, whatever is around them. */
const KEYWORDS: Record<string, SectionName> = {
    '🏁': 'start', '🔂': 'forIn', '🔁': 'repeatWhile', '⁉': 'callCallable', '👇': 'this', '⤴': 'super',
    '🕊': 'defineValueType', '🐊': 'defineProtocol', '🔘': 'defineEnum', '♻': 'deinitializer',
    '👍': 'true', '👎': 'false', '🤜': 'grouping', '🤛': 'grouping', '👐': 'shortCircuit', '🤝': 'shortCircuit',
    '😜': 'identity', '🍬': 'optional', '🤷': 'noValue', '🤷‍♀': 'noValue', '🤷‍♂': 'noValue', '🍺': 'unwrap',
    '🆗': 'handle', '🚨': 'raise', '🔺': 'reraise', '🚧': 'errorType', '🔶': 'namespace', '🔲': 'cast', '⚖': 'size',
    '⚪': 'something', '🔵': 'someobject', '◼': 'noReturn', '⚫': 'builtInType', '⬛': 'typeValue', '🍱': 'multiprotocol',
    '✴': 'reference', '🏮': 'shared', '📣': 'expectation', '📦': 'package', '📜': 'include', '🔗': 'link',
    '🌍': 'exported', '🔏': 'final', '✒': 'override', '🥯': 'inline', '⚠': 'deprecated', '🔑': 'required',
    '🔓': 'access', '🔒': 'access', '🔐': 'access', '📻': 'foreign', '☣': 'unsafe', '🍼': 'initParameter',
};

/** Keywords that the compiler reads as names where no keyword is expected, e.g. 🔒 in `🔒 mutex❗️` calls the method
 * 🔒 of 🔐, and 🚧 names a class of the s package. */
const NAMES = new Set(['🏁', '📦', '📜', '🔗', '🌍', '🔏', '✒', '🥯', '⚠', '🔑', '🔓', '🔒', '🔐', '📻', '🍼', '♻', '🚧',
                       '⚪', '🔵']);

const EMOJI = /\p{Extended_Pictographic}|[\u{1F1E6}-\u{1F1FF}]/u;
const NUMBER = /^(?:0,*[xX][0-9a-fA-F,]*|[+-]?,*[0-9][0-9,]*)(?:\.[0-9]+)?$/;
const segmenter = new Intl.Segmenter(undefined, { granularity: 'grapheme' });
/** Whether a grapheme ends a line like the lexer's line breaks. \r\n is one grapheme. */
const isLineBreak = (g: string) => g === '\n' || g === '\r\n' || g === '\u2028' || g === '\u2029';

/** Splits Emojicode source into pieces. Whitespace, and the parts of comments between pieces, are left out. */
export function lex(text: string): Piece[] {
    const graphemes = Array.from(segmenter.segment(text), (s) => ({ text: s.segment.replace(/️/g, ''), start: s.index }));
    const endOf = (i: number) => (i + 1 < graphemes.length ? graphemes[i + 1].start : text.length);
    const pieces: Piece[] = [];
    const add = (kind: PieceKind, from: number, to: number) => {
        pieces.push({ kind, text: text.slice(graphemes[from].start, endOf(to)).replace(/️/g, ''),
                      start: graphemes[from].start, end: endOf(to) });
    };
    /** Returns the index of the first grapheme from `i` on that is `g`, or the last index. */
    const find = (i: number, g: string) => {
        while (i < graphemes.length - 1 && graphemes[i].text !== g) i++;
        return i;
    };

    // Each entry is a string whose 🧲 interpolation is being lexed.
    let interpolations = 0;
    let i = 0;
    while (i < graphemes.length) {
        const g = graphemes[i].text;
        const next = graphemes[i + 1]?.text;
        if (/^\s+$/.test(g)) {
            i++;
        }
        else if (g === '💭' && next === '🔜') {
            let end = i + 2;
            while (end < graphemes.length - 1 && !(graphemes[end].text === '🔚' && graphemes[end + 1].text === '💭')) end++;
            end = Math.min(end + 1, graphemes.length - 1);
            add('comment', i, end);
            i = end + 1;
        }
        else if (g === '💭') {
            let end = i;
            while (end < graphemes.length - 1 && !isLineBreak(graphemes[end].text)) end++;
            add('comment', i, isLineBreak(graphemes[end].text) ? end - 1 : end);
            i = end + 1;
        }
        else if (g === '📗' || g === '📘') {
            const end = find(i + 1, g);
            add(g === '📗' ? 'documentation' : 'packageDocumentation', i, end);
            i = end + 1;
        }
        else if (g === '🔟') {
            add('symbol', i, Math.min(i + 1, graphemes.length - 1));
            i += 2;
        }
        else if (g === '🧲' && interpolations > 0) {
            // The end of an interpolation: back in the string.
            interpolations--;
            add('interpolation', i, i);
            i = lexString(i + 1);
        }
        else if (g === '🔤') {
            add('string', i, i);
            pieces[pieces.length - 1].opensString = true;
            i = lexString(i + 1);
        }
        else if (EMOJI.test(g)) {
            // 🔸 joins emoji into one name, e.g. 🚧🔸↕️.
            let end = i;
            while (graphemes[end + 1]?.text === '🔸' && end + 2 < graphemes.length && EMOJI.test(graphemes[end + 2].text)) {
                end += 2;
            }
            add('emoji', i, end);
            i = end + 1;
        }
        else {
            let end = i;
            while (end + 1 < graphemes.length && !/^\s+$/.test(graphemes[end + 1].text) && !EMOJI.test(graphemes[end + 1].text)) {
                end++;
            }
            add(NUMBER.test(text.slice(graphemes[i].start, endOf(end))) ? 'number' : 'word', i, end);
            i = end + 1;
        }
    }
    return pieces;

    /** Lexes the rest of a string from `i` and returns where code continues. */
    function lexString(i: number): number {
        let run = i;
        const flush = (to: number) => {
            if (to >= run) add('string', run, to);
        };
        while (i < graphemes.length) {
            const g = graphemes[i].text;
            if (g === '❌') {
                flush(i - 1);
                add('escape', i, Math.min(i + 1, graphemes.length - 1));
                i += 2;
                run = i;
            }
            else if (g === '🧲') {
                flush(i - 1);
                add('interpolation', i, i);
                interpolations++;
                return i + 1;
            }
            else if (g === '🔤') {
                flush(i);
                return i + 1;
            }
            else {
                i++;
            }
        }
        flush(graphemes.length - 1);
        return i;
    }
}

/** The code of a document, analysed to explain its tokens. */
export class LearnerDocument {
    readonly pieces: Piece[];
    private readonly tokens: Token[];
    /** The index of each piece in `tokens`, for pieces that are code. */
    private readonly tokenIndex = new Map<Piece, number>();
    /** The index of the first token of each line in `tokens`, by line. */
    private readonly lineStarts = new Map<number, number>();

    constructor(text: string) {
        this.pieces = lex(text);
        const lineOffsets = [0];
        for (let i = 0; i < text.length; i++) if (text[i] === '\n') lineOffsets.push(i + 1);
        const lineOf = (offset: number) => {
            let low = 0, high = lineOffsets.length - 1;
            while (low < high) {
                const mid = (low + high + 1) >> 1;
                if (lineOffsets[mid] <= offset) low = mid; else high = mid - 1;
            }
            return low;
        };

        this.tokens = [];
        const open: number[] = [];
        for (const piece of this.pieces) {
            if (piece.kind !== 'emoji' && piece.kind !== 'word' && piece.kind !== 'number') continue;
            const index = this.tokens.length;
            const token: Token = { ...piece, line: lineOf(piece.start), enclosing: open[open.length - 1] };
            this.tokens.push(token);
            this.tokenIndex.set(piece, index);
            if (!this.lineStarts.has(token.line)) this.lineStarts.set(token.line, index);

            const opener = token.text === '🍉' ? '🍇' : token.text === '🍆' ? '🐚🍿' : token.text === '🤛' ? '🤜' : undefined;
            if (['🍇', '🐚', '🍿', '🤜'].includes(token.text)) {
                open.push(index);
            }
            else if (opener !== undefined && open.length > 0 && opener.includes(this.tokens[open[open.length - 1]].text)) {
                const partner = open.pop()!;
                token.partner = partner;
                token.enclosing = open[open.length - 1];
                this.tokens[partner].partner = index;
            }
        }
    }

    /** Returns the piece at `offset` and the section that explains it, if any. `named` tells whether the piece may
     * name a type or method of a package, which only the language server knows. */
    explain(offset: number): { piece: Piece; section?: Section; named: boolean } | undefined {
        const piece = this.pieces.find((p) => p.start <= offset && offset < p.end);
        if (piece === undefined) return undefined;
        switch (piece.kind) {
            case 'string': case 'escape': case 'interpolation': case 'comment': case 'documentation':
            case 'packageDocumentation': case 'symbol': case 'number':
                return { piece, section: S[piece.kind], named: false };
            case 'word':
                return { piece, section: S.variable, named: false };
        }
        const name = this.classify(this.tokenIndex.get(piece)!);
        return { piece, section: name && S[name], named: name === undefined || NAMES.has(piece.text) };
    }

    /** Returns the section for the emoji token at `i`, or undefined if it is a name, e.g. of a method. */
    classify(i: number): SectionName | undefined {
        const token = this.tokens[i];
        const t = token.text;
        const prev = this.tokens[i - 1]?.text;
        const next = this.tokens[i + 1]?.text;

        if (prev === '🎍') return DECORATORS[t] ?? 'decorator';
        switch (t) {
            case '🎍': return (next !== undefined ? DECORATORS[next] : undefined) ?? 'decorator';
            case '🍇': case '🍉': return this.block(t === '🍇' ? i : token.partner);
            case '🐚': return 'generic';
            case '🍿': case '🍆': {
                const opener = t === '🍿' ? i : token.partner;
                if (opener === undefined) return t === '🍿' ? 'list' : undefined;
                if (this.tokens[opener].text === '🐚') return 'generic';
                return this.isDictionary(opener) ? 'dictionary' : 'list';
            }
            case '↪': return prev === '🙅' && this.tokens[i - 1].line === token.line ? 'elseIf' : 'if';
            case '🙅': {
                if (next === '↪' && this.tokens[i + 1].line === token.line) return 'elseIf';
                const before = this.tokens[i - 1];
                if (before?.text === '🍉' && before.partner !== undefined && this.head(before.partner)?.text === '🆗') {
                    return 'handleError';
                }
                return 'else';
            }
            case '↩': return prev === '↩' || next === '↩' ? 'returnNothing' : 'return';
            case '🐇':
                return this.head(i) === token ? 'defineClass' : 'typeMethod';
            case '🐊':
                return this.inTypeBody(i) ? 'conformance' : 'defineProtocol';
            case '❗': case '❓':
                if (this.head(i) === token) return t === '❗' ? 'declareMethod' : 'declareQuestion';
                return t === '❗' ? 'call' : 'callQuestion';
            case '🆕':
                if (prev === '🖍') return this.inTypeBody(i) ? 'instanceVariable' : 'mutable';
                return this.head(i) === token && this.isInitializerDeclaration(i) ? 'declareInitializer' : 'instantiate';
            case '🖍':
                if (next === '🆕') return this.inTypeBody(i) ? 'instanceVariable' : 'mutable';
                if (this.isDeclarationLine(i)) return 'mutating';
                return prev === '➡' ? 'assign' : 'mutable';
            case '▶':
                return this.namesInitializer(i) ? 'namedInitializer' : 'operator';
            case '🚧':
                // 🚧 is also the error type of the s package, e.g. in 🆕🚧 and in 🚧🚧.
                if (prev === '🚧' || (prev === '🆕' && !(this.head(i - 1) === this.tokens[i - 1] &&
                                                       this.isInitializerDeclaration(i - 1)))) {
                    return undefined;
                }
                return 'errorType';
            case '➡': return this.arrow(i);
            case '⬅':
                // In code, ⬅️ is always followed by an operator, e.g. x ⬅️➕ 1.
                return prev !== undefined && this.lineTokens(i).some((x) => x.text === '🆕') && this.inTypeBody(i)
                    ? 'defaultValue' : 'operatorAssign';
        }
        if (OPERATORS.has(t)) {
            if (prev === '⬅') return 'operatorAssign';
            return 'operator';
        }
        return KEYWORDS[t];
    }

    /** The tokens on the line of token `i`. */
    private lineTokens(i: number): Token[] {
        const line = this.tokens[i].line;
        const result: Token[] = [];
        for (let j = this.lineStarts.get(line)!; j < this.tokens.length && this.tokens[j].line === line; j++) {
            result.push(this.tokens[j]);
        }
        return result;
    }

    /** The token that starts the statement or declaration on the line of token `i`, after 🍉, attributes and
     * decorators. */
    private head(i: number): Token | undefined {
        const line = this.lineTokens(i);
        let j = 0;
        while (j < line.length) {
            const t = line[j].text;
            const after = line[j + 1]?.text;
            if ((t === '🍉' && !CONTINUATIONS.includes(after ?? '')) || (MODIFIERS.has(t) && !(t === '☣' && after === '🍇')) ||
                (t === '🖍' && after !== '🆕') ||
                (t === '🐇' && this.marksTypeMethod(line, j + 1))) {
                j++;
            }
            else if (t === '🎍') {
                j += 2;
            }
            else {
                break;
            }
        }
        return line[j];
    }

    /** Whether the tokens of `line` from `j` on are attributes of a method, i.e. a 🐇 before them makes it a type
     * method. */
    private marksTypeMethod(line: Token[], j: number): boolean {
        while (j < line.length && (MODIFIERS.has(line[j].text) || line[j].text === '🖍' || line[j].text === '🎍')) {
            j += line[j].text === '🎍' ? 2 : 1;
        }
        return line[j]?.text === '❗' || line[j]?.text === '❓';
    }

    /** Whether the ▶️ at `i` names an initializer, e.g. in 🆕🔡▶️👂🏼❗️ or 🆕 ▶️ 🦈 🍇, rather than comparing. */
    private namesInitializer(i: number): boolean {
        let j = i - 1;
        const before = this.tokens[j];
        if (before?.text === '🆕') return true;
        // Otherwise ▶️ follows the type of an instantiation: 🆕, maybe 🔶 and a namespace, the type and maybe its
        // generic arguments.
        if (before?.text === '🍆' && before.partner !== undefined) j = before.partner - 1;
        j--;
        if (this.tokens[j]?.text !== '🆕' && this.tokens[j - 1]?.text === '🔶') j -= 2;
        return this.tokens[j]?.text === '🆕';
    }

    /** Whether the line of token `i` declares a method or initializer. */
    private isDeclarationLine(i: number): boolean {
        const head = this.head(i);
        if (head === undefined) return false;
        const index = this.tokens.indexOf(head);
        return head.text === '❗' || head.text === '❓' || (head.text === '🆕' && this.isInitializerDeclaration(index));
    }

    /** Whether the 🆕 at `i`, which starts its line, declares an initializer rather than creating an instance. */
    private isInitializerDeclaration(i: number): boolean {
        const next = this.tokens[i + 1];
        if (next === undefined || next.line !== this.tokens[i].line || !this.inTypeBody(i)) return false;
        return ['🍇', '▶', '🎍', '🍼', '🚧'].includes(next.text) || next.kind === 'word';
    }

    /** Whether token `i` is directly in the body of a type, not in a method. */
    private inTypeBody(i: number): boolean {
        const enclosing = this.tokens[i].enclosing;
        return enclosing !== undefined && this.block(enclosing) === 'typeBody';
    }

    /** Whether the collection literal opened at `opener` is a dictionary literal. */
    private isDictionary(opener: number): boolean {
        const end = this.tokens[opener].partner ?? this.tokens.length;
        for (let j = opener + 1; j < end; j++) {
            if (this.tokens[j].text === '➡' && this.tokens[j].enclosing === opener) return true;
        }
        return false;
    }

    /** The section for the 🍇 at `opener`, and its 🍉. */
    private block(opener: number | undefined): SectionName {
        if (opener === undefined) return 'block';
        const token = this.tokens[opener];
        const head = this.head(opener);
        const close = token.partner;
        const afterClose = close !== undefined ? this.tokens[close + 1] : undefined;
        // A closure is an expression, so a call or an operator can follow it.
        const continues = afterClose !== undefined && afterClose.line === this.tokens[close!].line &&
            CONTINUATIONS.includes(afterClose.text);
        const previous = this.tokens[opener - 1];
        if (head === token && token.enclosing === undefined && !continues && previous !== undefined && previous.text !== '🍉') {
            // The body of a type or of 🏁 can start on the line after the declaration.
            const declaration = this.head(opener - 1)?.text;
            if (declaration === '🏁') return 'start';
            if (declaration === '🐇' || declaration === '🕊' || declaration === '🐊' || declaration === '🔘') return 'typeBody';
        }
        const heads = ['↪', '🙅', '🔂', '🔁', '🆗', '🏁', '❗', '❓', '🆕', '♻', '🐇', '🕊', '🐊', '🔘', '☣'];
        if (head === undefined || head === token || !heads.includes(head.text) || continues || token.enclosing !== head.enclosing) {
            return 'closure';
        }
        const headIndex = this.tokens.indexOf(head);
        if (head.text === '🆕' && !this.isInitializerDeclaration(headIndex)) return 'closure';
        switch (head.text) {
            case '🏁': return 'start';
            case '♻': return 'deinitializer';
            case '☣': return 'unsafe';
            case '🐇': case '🕊': case '🐊': case '🔘': return 'typeBody';
            case '❗': case '❓': case '🆕': {
                // 🍇🔢🍉 in a declaration before its body is a callable type.
                const laterBlock = this.lineTokens(opener).some((x) => x.text === '🍇' && x.start > token.start &&
                                                                        x.enclosing === token.enclosing);
                if (laterBlock) return 'callableType';
                return head.text === '🆕' ? 'initializerBody' : 'methodBody';
            }
            default: return 'block';
        }
    }

    /** The section for ➡️ at `i`. */
    private arrow(i: number): SectionName {
        const token = this.tokens[i];
        // ➡️ 🖍🆕 x declares a mutable variable, and ➡️ 🖍 x changes one.
        if (this.tokens[i + 1]?.text === '🖍') return this.tokens[i + 2]?.text === '🆕' ? 'mutable' : 'assign';
        if (token.enclosing !== undefined) {
            const enclosing = this.tokens[token.enclosing];
            if (enclosing.text === '🍿') return 'dictionary';
            if (enclosing.text === '🍇' && enclosing.line === token.line && this.block(token.enclosing) === 'closure') {
                // Only the parameters and ➡️ come before the code of a closure.
                const code = this.tokens.slice(token.enclosing + 1, i).some((x) => ['❗', '❓', '⁉', '➡'].includes(x.text));
                if (!code) return 'closureReturnType';
            }
        }
        const head = this.head(i);
        if (this.isDeclarationLine(i) && head !== undefined && head.enclosing === token.enclosing) return 'returnType';
        return 'constant';
    }
}
