import * as vscode from 'vscode';
import { lex } from './learnerDocs';

// VS Code cannot auto-close 🍇 with the autoClosingPairs of the language configuration: it looks up the pair by the
// last UTF-16 code unit of the opening text, but gets the whole typed emoji, which is two code units. This does what
// the editor does for braces instead: typing 🍇 in code inserts 🍉 after the cursor, typing 🍉 before an inserted 🍉
// types over it, and deleting 🍇 deletes the 🍉 inserted with it.

const PAIRS: Record<string, string> = { '🍇': '🍉', '🤜': '🤛', '🐚': '🍆', '🍿': '🍆', '📗': '📗', '📘': '📘', '🔤': '🔤' };
/** The pieces that an opening emoji starts, if it is not code itself. */
const STARTS: Record<string, string> = { '📗': 'documentation', '📘': 'packageDocumentation', '🔤': 'string' };
/** The text after which a closing emoji is inserted, besides whitespace, like editor.autoCloseBefore. */
const CLOSE_BEFORE = /^(?:\s|$|🍉|🤛|🍆|❗|❓|⁉)/u;

/** A closing emoji that was inserted with its opening one. Offsets are in UTF-16 code units. */
interface Inserted {
    document: vscode.TextDocument;
    open: number;
    opening: string;
    close: number;
    closing: string;
}

export function registerAutoClose(context: vscode.ExtensionContext) {
    let inserted: Inserted | undefined;
    /** Whether the changes of the document are made by this, not by the user. */
    let editing = false;

    const edit = async (editor: vscode.TextEditor, change: (builder: vscode.TextEditorEdit) => void,
                        selections?: vscode.Selection[]) => {
        editing = true;
        try {
            await editor.edit(change, { undoStopBefore: false, undoStopAfter: false });
            if (selections) editor.selections = selections;
        }
        finally {
            editing = false;
        }
    };

    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(async (event) => {
        const document = event.document;
        const editor = vscode.window.activeTextEditor;
        if (editing || event.reason !== undefined || document.languageId !== 'emojicode' || editor?.document !== document ||
            event.contentChanges.length === 0) {
            return;
        }
        const config = vscode.workspace.getConfiguration('editor', document);
        const tracked = inserted?.document === document ? inserted : undefined;
        inserted = undefined;

        if (event.contentChanges.length === 1 && tracked !== undefined) {
            const change = event.contentChanges[0];
            const typed = change.text.replace(/️/g, '');
            if (change.rangeLength === 0 && typed === tracked.closing && change.rangeOffset === tracked.close &&
                config.get('autoClosingOvertype') !== 'never') {
                // Type over the inserted 🍉, which the change moved behind the typed one.
                const at = tracked.close + change.text.length;
                await edit(editor, (builder) => builder.delete(range(document, at, at + tracked.closing.length)));
                return;
            }
            if (change.text === '' && change.rangeOffset === tracked.open && change.rangeLength === tracked.opening.length &&
                tracked.close === tracked.open + tracked.opening.length && config.get('autoClosingDelete') !== 'never') {
                // The 🍇 was deleted while the 🍉 still followed it.
                await edit(editor, (builder) => builder.delete(range(document, tracked.open, tracked.open + tracked.closing.length)));
                return;
            }
            if (change.rangeOffset >= tracked.open + tracked.opening.length &&
                change.rangeOffset + change.rangeLength <= tracked.close && !/[\r\n]/.test(change.text)) {
                // Typing between the pair on its line keeps the 🍉 as the one to type over.
                inserted = { ...tracked, close: tracked.close + change.text.length - change.rangeLength };
                return;
            }
        }

        const mode = config.get<string>('autoClosingBrackets', 'languageDefined');
        if (mode === 'never') return;
        // Each cursor that typed an opening emoji, by where the change put it.
        const openings = event.contentChanges.map((change) => {
            const opening = change.text.replace(/️/g, '');
            return PAIRS[opening] !== undefined ? { offset: change.rangeOffset, length: change.rangeLength, text: change.text, opening } : undefined;
        });
        if (openings.some((opening) => opening === undefined)) return;

        const pieces = lex(document.getText());
        const closes: { at: vscode.Position; opening: string; open: number }[] = [];
        // The offsets of the changes are before the changes; later changes of the event are earlier in the text.
        const sorted = [...openings].sort((a, b) => a!.offset - b!.offset);
        let shift = 0;
        for (const opening of sorted) {
            const open = opening!.offset + shift;
            shift += opening!.text.length - opening!.length;
            const after = open + opening!.text.length;
            const piece = pieces.find((p) => p.start <= open && open < p.end);
            // Only in code, not in a string or comment. A 🔤 or 📗 opens a string or documentation comment if it
            // starts at it, and otherwise ends one.
            const start = STARTS[opening!.opening];
            if (start !== undefined ? piece?.kind !== start || piece.start !== open : piece?.kind !== 'emoji') return;
            const position = document.positionAt(after);
            const rest = document.lineAt(position.line).text.slice(position.character);
            if (mode === 'beforeWhitespace' ? !/^(?:\s|$)/.test(rest) : !CLOSE_BEFORE.test(rest)) return;
            closes.push({ at: position, opening: opening!.opening, open });
        }

        // The change event comes before the cursors move past what was typed, so they are put between the pairs.
        await edit(editor, (builder) => {
            for (const close of closes) builder.insert(close.at, PAIRS[close.opening]);
        }, closes.map((close) => new vscode.Selection(close.at, close.at)));
        if (closes.length === 1) {
            const close = closes[0];
            inserted = { document, open: close.open, opening: close.opening, close: document.offsetAt(close.at),
                         closing: PAIRS[close.opening] };
        }
    }));
}

function range(document: vscode.TextDocument, start: number, end: number): vscode.Range {
    return new vscode.Range(document.positionAt(start), document.positionAt(end));
}
