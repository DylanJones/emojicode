// Types into a real VS Code editor to check auto-closing. Run with test/integration/run.sh, which starts VS Code with
// this extension and a fresh profile.
const assert = require('assert');
const vscode = require('vscode');

async function editorWith(text) {
    const document = await vscode.workspace.openTextDocument({ language: 'emojicode', content: text });
    const editor = await vscode.window.showTextDocument(document);
    const end = document.positionAt(text.length);
    editor.selection = new vscode.Selection(end, end);
    return editor;
}

async function type(text) {
    await vscode.commands.executeCommand('type', { text });
    // The closing emoji is inserted after the change event, so let it run.
    await new Promise((resolve) => setTimeout(resolve, 100));
}

function state(editor) {
    const document = editor.document;
    const offset = document.offsetAt(editor.selection.active);
    const text = document.getText();
    return text.slice(0, offset) + '|' + text.slice(offset);
}

const cases = {
    async 'closes a block'() {
        const editor = await editorWith('🏁 ');
        await type('🍇');
        assert.strictEqual(state(editor), '🏁 🍇|🍉');
    },
    async 'types over the inserted 🍉'() {
        const editor = await editorWith('🏁 ');
        await type('🍇');
        await type('🍉');
        assert.strictEqual(state(editor), '🏁 🍇🍉|');
    },
    async 'types over after typing inside'() {
        const editor = await editorWith('↪️ ');
        await type('🍇');
        await type('x');
        await type('🍉');
        assert.strictEqual(state(editor), '↪️ 🍇x🍉|');
    },
    async 'deletes the inserted 🍉 with the 🍇'() {
        const editor = await editorWith('🏁 ');
        await type('🍇');
        await vscode.commands.executeCommand('deleteLeft');
        await new Promise((resolve) => setTimeout(resolve, 100));
        assert.strictEqual(state(editor), '🏁 |');
    },
    async 'closes 🤜'() {
        const editor = await editorWith('x ➡️ ');
        await type('🤜');
        assert.strictEqual(state(editor), 'x ➡️ 🤜|🤛');
    },
    async 'closes generic arguments'() {
        const editor = await editorWith('🍨');
        await type('🐚');
        assert.strictEqual(state(editor), '🍨🐚|🍆');
        await type('🔢');
        await type('🍆');
        assert.strictEqual(state(editor), '🍨🐚🔢🍆|');
    },
    async 'nested generic arguments'() {
        const editor = await editorWith('🍯🐚🔡 🍨');
        editor.selection = new vscode.Selection(0, 9, 0, 9);
        await type('🐚');
        assert.strictEqual(state(editor), '🍯🐚🔡 🍨🐚|🍆');
    },
    async 'closes a collection literal'() {
        const editor = await editorWith('');
        await type('🍿');
        assert.strictEqual(state(editor), '🍿|🍆');
        await type(' 1 2 ');
        await type('🍆');
        assert.strictEqual(state(editor), '🍿 1 2 🍆|');
    },
    async 'closes a documentation comment'() {
        const editor = await editorWith('🐇 🐟 🍇\n  ');
        await type('📗');
        assert.strictEqual(state(editor), '🐇 🐟 🍇\n  📗|📗');
        await type(' Swims. ');
        await type('📗');
        assert.strictEqual(state(editor), '🐇 🐟 🍇\n  📗 Swims. 📗|');
    },
    async 'closes a package documentation comment'() {
        const editor = await editorWith('');
        await type('📘');
        assert.strictEqual(state(editor), '📘|📘');
    },
    async 'does not close when 📗 ends a documentation comment'() {
        const editor = await editorWith('📗 Swims. ');
        await type('📗');
        assert.strictEqual(state(editor), '📗 Swims. 📗|');
    },
    async 'closes 📗 before another documentation comment'() {
        const editor = await editorWith('\n📗 Next. 📗\n❗️ 🐽 🍇🍉');
        editor.selection = new vscode.Selection(0, 0, 0, 0);
        await type('📗');
        assert.strictEqual(state(editor), '📗|📗\n📗 Next. 📗\n❗️ 🐽 🍇🍉');
    },
    async 'no 📗 in a string'() {
        const editor = await editorWith('😀 🔤Read ');
        await type('📗');
        assert.strictEqual(state(editor), '😀 🔤Read 📗|');
    },
    async 'closes a string'() {
        const editor = await editorWith('😀 ');
        await type('🔤');
        assert.strictEqual(state(editor), '😀 🔤|🔤');
        await type('Hi');
        await type('🔤');
        assert.strictEqual(state(editor), '😀 🔤Hi🔤|');
    },
    async 'does not close when 🔤 ends a string'() {
        const editor = await editorWith('😀 🔤Hi');
        await type('🔤');
        assert.strictEqual(state(editor), '😀 🔤Hi🔤|');
    },
    async 'does not close an escaped 🔤'() {
        const editor = await editorWith('😀 🔤Hi ❌');
        await type('🔤');
        assert.strictEqual(state(editor), '😀 🔤Hi ❌🔤|');
    },
    async 'closes a string in an interpolation'() {
        const editor = await editorWith('😀 🔤a 🧲');
        await type('🔤');
        assert.strictEqual(state(editor), '😀 🔤a 🧲🔤|🔤');
    },
    async 'closes a string before a call'() {
        const editor = await editorWith('😀 ❗️');
        editor.selection = new vscode.Selection(0, 3, 0, 3);
        await type('🔤');
        assert.strictEqual(state(editor), '😀 🔤|🔤❗️');
    },
    async 'not in a string'() {
        const editor = await editorWith('😀 🔤Grapes ');
        await type('🍇');
        assert.strictEqual(state(editor), '😀 🔤Grapes 🍇|');
    },
    async 'not in a closed string'() {
        const editor = await editorWith('😀 🔤Grapes 🔤❗️');
        editor.selection = new vscode.Selection(0, 12, 0, 12);
        await type('🍇');
        assert.strictEqual(state(editor), '😀 🔤Grapes 🍇|🔤❗️');
    },
    async 'in an interpolation'() {
        const editor = await editorWith('😀 🔤a 🧲');
        await type('🍇');
        assert.strictEqual(state(editor), '😀 🔤a 🧲🍇|🍉');
    },
    async 'not in a comment'() {
        const editor = await editorWith('💭 ');
        await type('🍇');
        assert.strictEqual(state(editor), '💭 🍇|');
    },
    async 'not in a documentation comment'() {
        const editor = await editorWith('📗 Grapes ');
        await type('🍇');
        assert.strictEqual(state(editor), '📗 Grapes 🍇|');
    },
    async 'not before a word'() {
        const editor = await editorWith('x');
        editor.selection = new vscode.Selection(0, 0, 0, 0);
        await type('🍇');
        assert.strictEqual(state(editor), '🍇|x');
    },
    async 'enter between the pair indents'() {
        const editor = await editorWith('🏁 ');
        await type('🍇');
        await type('\n');
        assert.strictEqual(state(editor), '🏁 🍇\n    |\n🍉');
    },
};

exports.run = async function () {
    await vscode.workspace.getConfiguration('editor').update('insertSpaces', true, vscode.ConfigurationTarget.Global);
    const failures = [];
    for (const [name, test] of Object.entries(cases)) {
        try {
            await test();
            console.log(`✔ ${name}`);
        }
        catch (error) {
            console.log(`✘ ${name}: ${JSON.stringify(error.actual)} expected ${JSON.stringify(error.expected)}`);
            failures.push(name);
        }
        await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');
    }
    if (failures.length > 0) throw new Error(`${failures.length} failed`);
};
