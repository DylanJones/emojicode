// Tokenizes Emojicode snippets with the TextMate grammar, using the same engine as VS Code, and checks the scopes.
// Run with `npm test`.
const fs = require('fs');
const path = require('path');
const assert = require('assert');
const oniguruma = require('vscode-oniguruma');
const textmate = require('vscode-textmate');

// Each case is a line of code and the scope expected for substrings of it. A substring is found by its first
// occurrence, or its n-th with "substring#n". The scope must be the innermost scope of every character.
const cases = [
    ['🏁 🍇', { '🏁': 'keyword.other.start', '🍇': 'punctuation.section.block.begin' }],
    ['  😀 🔤Hello world!🔤❗️', { 'Hello world!': 'string.quoted', '❗️': 'punctuation.terminator.mood' }],
    ['💭 a comment 🍇', { ' a comment 🍇': 'comment.line' }],
    ['💭🔜 many 🍇 lines 🔚💭 x', { ' many 🍇 lines ': 'comment.block', 'x': 'variable.other' }],
    ['📗 Docs 📗', { ' Docs ': 'comment.block.documentation' }],
    ['🔤a ❌n b ❌🔤 c❌x🔤', { '❌n': 'constant.character.escape', '❌🔤': 'constant.character.escape',
                              '❌x': 'invalid.illegal.escape' }],
    ['🔤Inserted 🧲🐽row 1❗️🧲 rows🔤', { 'Inserted ': 'string.quoted', 'row': 'variable.other', '1': 'constant.numeric',
                                          ' rows': 'string.quoted' }],
    ['🐇 🐟 🍇', { '🐇': 'storage.type', '🐟': 'entity.name.type' }],
    ['🌍 🕊 🔶🌊🔢 🍇', { '🌍': 'storage.modifier', '🌊': 'entity.name.namespace', '🔢': 'entity.name.type' }],
    ['🐇❗️ 🎥 🚧💥 🍇', { '🐇': 'storage.modifier', '❗️': 'storage.type.function', '🎥': 'entity.name.function' }],
    ['  ☣️🖍❗️ 🐽 i 🔢 ➡️ 🔢 🍇', { '☣️': 'storage.modifier', '🐽': 'entity.name.function', 'i': 'variable.other',
                                   '➡️': 'keyword.operator.assignment' }],
    ['  🎍🌊 🐇 ☣️ ❗️ 🧮 x 🔶🌊🔢 📻 🔤abs🔤', { '🌊': 'storage.modifier.decorator', '🧮': 'entity.name.function' }],
    ['  🆕 ▶️ 🔡 name 🔡 🍇', { '▶️': 'storage.type.function', '🔡': 'entity.name.function' }],
    ['↪️ a ▶️🙌 -1,000.5 🍇 🍉 🙅↪️ 👎 🍇 🍉 🙅 🍇 🍉', { '↪️': 'keyword.control', '▶️🙌': 'keyword.operator',
        '-1,000.5': 'constant.numeric', '🙅↪️': 'keyword.control', '👎': 'constant.language.boolean',
        '🙅#2': 'keyword.control' }],
    ['🔂 i 🆕⏩ 0 0xFF❗️ 🍇', { '🔂': 'keyword.control', '🆕': 'keyword.other.new', '0xFF': 'constant.numeric' }],
    ['x-1 ➡️ 🖍🆕 a2', { 'x-1': 'variable.other', 'a2': 'variable.other', '🖍': 'storage.modifier' }],
    ['↩️ 👇', { '↩️': 'keyword.control', '👇': 'variable.language.this' }],
    ['📦 sqlite 🏠', { '📦': 'keyword.other.import', 'sqlite': 'variable.other' }],
];

async function main() {
    const wasm = fs.readFileSync(require.resolve('vscode-oniguruma/release/onig.wasm'));
    await oniguruma.loadWASM(wasm.buffer);
    const registry = new textmate.Registry({
        onigLib: Promise.resolve({
            createOnigScanner: (patterns) => new oniguruma.OnigScanner(patterns),
            createOnigString: (s) => new oniguruma.OnigString(s),
        }),
        loadGrammar: async () => textmate.parseRawGrammar(
            fs.readFileSync(path.join(__dirname, '..', 'syntaxes', 'emojicode.tmLanguage.json'), 'utf8'),
            'emojicode.tmLanguage.json'),
    });
    const grammar = await registry.loadGrammar('source.emojicode');

    let failures = 0;
    for (const [line, expectations] of cases) {
        const { tokens } = grammar.tokenizeLine(line, textmate.INITIAL);
        const scopeAt = (i) => tokens.find((t) => t.startIndex <= i && i < t.endIndex).scopes;
        for (const [key, expected] of Object.entries(expectations)) {
            const match = /^(.+)#(\d+)$/.exec(key);
            const [substring, occurrence] = match ? [match[1], Number(match[2])] : [key, 1];
            let start = -1;
            for (let n = 0; n < occurrence; n++) {
                start = line.indexOf(substring, start + 1);
            }
            assert(start >= 0, `${substring} not in ${line}`);
            for (let i = start; i < start + substring.length; i++) {
                const scopes = scopeAt(i);
                const innermost = scopes[scopes.length - 1];
                if (!innermost.startsWith(expected + '.') && innermost !== expected) {
                    console.error(`✘ ${line}\n  ${substring}: expected ${expected}, got ${scopes.join(' ')}`);
                    failures++;
                    break;
                }
            }
        }
    }
    console.log(`${failures ? '✘' : '✔'} ${cases.length} lines tokenized, ${failures} failures`);

    // Every source file in the repository must end outside of any string, comment or interpolation. This catches
    // begin/end rules that run away.
    const root = path.join(__dirname, '..', '..', '..');
    const files = [];
    const walk = (dir) => {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, entry.name);
            if (entry.isDirectory() && !['node_modules', 'build', '.git', '.claude'].includes(entry.name)) {
                walk(full);
            }
            else if (/\.(emojic|🍇|emojii)$|^🏛$/u.test(entry.name) && !full.includes(`${path.sep}reject${path.sep}`)) {
                files.push(full);
            }
        }
    };
    walk(root);
    let unclosed = 0;
    for (const file of files) {
        let stack = textmate.INITIAL;
        for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
            stack = grammar.tokenizeLine(line, stack).ruleStack;
        }
        if (stack.depth > 1) {
            console.error(`✘ ${path.relative(root, file)} ends inside ${stack.nameScopesList?.scopePath?.scopeName ?? 'a region'}`);
            unclosed++;
        }
    }
    console.log(`${unclosed ? '✘' : '✔'} ${files.length} files tokenized, ${unclosed} end inside a region`);
    if (failures > 0 || unclosed > 0) {
        process.exit(1);
    }
}

main().catch((error) => { console.error(error); process.exit(1); });
