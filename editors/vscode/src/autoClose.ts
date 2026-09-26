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
    /** The inserted closing emoji that can still be typed over, the innermost last: an opening emoji typed between
     * an inserted pair is closed inside it. */
    let inserted: Inserted[] = [];
    /** Whether the changes of the document are made by this, not by the user. */
    let editing = false;

    /** Makes a change and returns whether it was made. It is not if the document changed since the event, e.g. as
     * the user typed on, and then the cursors are left where they are. */
    const edit = async (editor: vscode.TextEditor, change: (builder: vscode.TextEditorEdit) => void,
                        selections?: () => vscode.Selection[]) => {
        editing = true;
        try {
            const applied = await editor.edit(change, { undoStopBefore: false, undoStopAfter: false });
            if (applied && selections) editor.selections = selections();
            return applied;
        }
        finally {
            editing = false;
        }
    };

    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(async (event) => {
        const document = event.document;
        if (editing || document.languageId !== 'emojicode' || event.contentChanges.length === 0) return;
        // Any change of the document moves its inserted closing emoji, so they are only kept by the cases below.
        const tracked = inserted.filter((pair) => pair.document === document);
        inserted = inserted.filter((pair) => pair.document !== document);
        const editor = vscode.window.activeTextEditor;
        if (event.reason !== undefined || editor?.document !== document) return;
        // Typing in one document forgets the closing emoji inserted in others.
        inserted = [];
        const config = vscode.workspace.getConfiguration('editor', document);

        if (event.contentChanges.length === 1 && tracked.length > 0) {
            const change = event.contentChanges[0];
            const typed = change.text.replace(/️/g, '');
            // The innermost pair whose 🍉 is right after the change.
            let index = tracked.length - 1;
            while (index >= 0 && tracked[index].close !== change.rangeOffset) index--;
            const over = tracked[index];
            // ❌🔤 is a 🔤 in the string, not the end of it.
            const escaped = over?.closing === '🔤' && escapes(document, over.open + over.opening.length, change.rangeOffset);
            if (over !== undefined && change.rangeLength === 0 && typed === over.closing && !escaped &&
                config.get('autoClosingOvertype') !== 'never') {
                // Type over the inserted 🍉, which the change moved behind the typed one.
                const at = over.close + change.text.length;
                const closing = range(document, at, at + over.closing.length);
                if (document.getText(closing) === over.closing && await edit(editor, (builder) => builder.delete(closing))) {
                    inserted = tracked.slice(0, index)
                        .map((pair) => ({ ...pair, close: pair.close + change.text.length - over.closing.length }));
                }
                return;
            }
            const innermost = tracked[tracked.length - 1];
            if (change.text === '' && change.rangeOffset === innermost.open && change.rangeLength === innermost.opening.length &&
                innermost.close === innermost.open + innermost.opening.length && config.get('autoClosingDelete') !== 'never') {
                // The 🍇 was deleted while the 🍉 still followed it.
                const closing = range(document, innermost.open, innermost.open + innermost.closing.length);
                if (document.getText(closing) === innermost.closing && await edit(editor, (builder) => builder.delete(closing))) {
                    const removed = change.rangeLength + innermost.closing.length;
                    inserted = tracked.slice(0, -1).map((pair) => ({ ...pair, close: pair.close - removed }));
                }
                return;
            }
            if (!/[\r\n]/.test(change.text)) {
                // Typing between a pair on its line keeps the 🍉 as the one to type over.
                inserted = tracked.filter((pair) => change.rangeOffset >= pair.open + pair.opening.length &&
                                                    change.rangeOffset + change.rangeLength <= pair.close)
                    .map((pair) => ({ ...pair, close: pair.close + change.text.length - change.rangeLength }));
            }
        }

        const mode = config.get<string>('autoClosingBrackets', 'languageDefined');
        if (mode === 'never') return;
        // Only what is typed at the cursors, not e.g. the matches that Replace All changes elsewhere. The change event
        // comes before the cursors move, so what is typed ends where a selection ends.
        const atCursors = event.contentChanges.every((change) =>
            editor.selections.some((selection) => selection.end.isEqual(change.range.end)));
        if (!atCursors) return;
        // Each cursor that typed an opening emoji, by where the change put it.
        const openings = event.contentChanges.map((change) => {
            const opening = change.text.replace(/️/g, '');
            return PAIRS[opening] !== undefined ? { offset: change.rangeOffset, length: change.rangeLength, text: change.text, opening } : undefined;
        });
        if (openings.some((opening) => opening === undefined)) return;

        const pieces = lex(document.getText());
        const closes: { at: number; opening: string; open: number }[] = [];
        // The offsets of the changes are before the changes; later changes of the event are earlier in the text.
        const sorted = [...openings].sort((a, b) => a!.offset - b!.offset);
        let shift = 0;
        for (const opening of sorted) {
            const open = opening!.offset + shift;
            shift += opening!.text.length - opening!.length;
            const after = open + opening!.text.length;
            const piece = pieces.find((p) => p.start <= open && open < p.end);
            // Only in code, not in a string or comment. A 🔤 or 📗 opens a string or documentation comment if it
            // starts one, and otherwise ends one.
            const start = STARTS[opening!.opening];
            const opens = start === 'string' ? piece?.opensString === true
                : start !== undefined ? piece?.kind === start && piece.start === open : piece?.kind === 'emoji';
            if (!opens) return;
            const position = document.positionAt(after);
            const rest = document.lineAt(position.line).text.slice(position.character);
            if (mode !== 'always' && !(mode === 'beforeWhitespace' ? /^(?:\s|$)/ : CLOSE_BEFORE).test(rest)) return;
            closes.push({ at: after, opening: opening!.opening, open });
        }

        // The change event comes before the cursors move past what was typed, so they are put between the pairs,
        // each behind the closing emoji inserted before it.
        const applied = await edit(editor, (builder) => {
            for (const close of closes) builder.insert(document.positionAt(close.at), PAIRS[close.opening]);
        }, () => {
            let moved = 0;
            return closes.map((close) => {
                const position = document.positionAt(close.at + moved);
                moved += PAIRS[close.opening].length;
                return new vscode.Selection(position, position);
            });
        });
        if (!applied || closes.length !== 1) {
            inserted = [];
            return;
        }
        const close = closes[0];
        const closing = PAIRS[close.opening];
        // The pairs around it now close after its 🍉.
        inserted = [...inserted.map((pair) => ({ ...pair, close: pair.close + closing.length })),
                    { document, open: close.open, opening: close.opening, close: close.at, closing }];
    }));
}

function range(document: vscode.TextDocument, start: number, end: number): vscode.Range {
    return new vscode.Range(document.positionAt(start), document.positionAt(end));
}

/** Whether the text from `start` to `end` ends in an ❌ that escapes what follows it in a string. */
function escapes(document: vscode.TextDocument, start: number, end: number): boolean {
    return /❌*$/.exec(document.getText(range(document, start, end)))![0].length % 2 === 1;
}
