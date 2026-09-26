import * as vscode from 'vscode';
import { LanguageClient } from 'vscode-languageclient/node';
import { LearnerDocument, Piece, Section } from './learnerDocs';

const ENABLED = 'learnerDocs.enabled';
const BASE_URL = 'learnerDocs.baseUrl';

/** Where the language server's package documentation describes a symbol, from its emojicode/docs request. */
interface DocsLocation {
    path: string;
}

/** Links tokens to the section of the documentation that explains them, in a hover, while learner docs are on. */
export function registerLearnerDocs(context: vscode.ExtensionContext, getClient: () => LanguageClient | undefined) {
    const documents = new Map<string, { version: number; analysis: LearnerDocument }>();
    const analyse = (document: vscode.TextDocument) => {
        const key = document.uri.toString();
        let cached = documents.get(key);
        if (cached?.version !== document.version) {
            cached = { version: document.version, analysis: new LearnerDocument(document.getText()) };
            documents.set(key, cached);
        }
        return cached.analysis;
    };

    /** Returns the piece at `offset` and the URL of the documentation that explains it. */
    const locate = async (document: vscode.TextDocument, offset: number, token?: vscode.CancellationToken) => {
        const explained = analyse(document).explain(offset);
        if (explained === undefined) return undefined;
        const { piece, section, named } = explained;
        // An emoji that names a type or method, even one that is a keyword elsewhere like 🔒, is explained by the
        // documentation of its package, which only the language server knows.
        const client = getClient();
        if (named && client !== undefined && client.isRunning()) {
            const params = client.code2ProtocolConverter.asTextDocumentPositionParams(document, document.positionAt(piece.start));
            // Without a token, sendRequest would send [params, null] as the parameters.
            const request = token === undefined ? client.sendRequest<DocsLocation | null>('emojicode/docs', params)
                : client.sendRequest<DocsLocation | null>('emojicode/docs', params, token);
            const location = await request.catch(() => null);
            if (location) {
                const summary = `${piece.text} is part of a package. Its documentation describes what it does.`;
                return { piece, url: url(location.path),
                         section: { summary, title: `${piece.text} in the package documentation`, path: location.path } };
            }
        }
        return section && { piece, section, url: url(section.path) };
    };

    context.subscriptions.push(
        vscode.languages.registerHoverProvider({ language: 'emojicode' }, {
            async provideHover(document, position, token) {
                if (!enabled()) return undefined;
                const found = await locate(document, document.offsetAt(position), token);
                return found && new vscode.Hover(hoverText(found.section, found.url), range(document, found.piece));
            },
        }),
        vscode.commands.registerCommand('emojicode.toggleLearnerDocs', async () => {
            const config = vscode.workspace.getConfiguration('emojicode');
            // Change the setting where it is set, so that a workspace setting does not override the change.
            const inspected = config.inspect<boolean>(ENABLED);
            const target = inspected?.workspaceFolderValue !== undefined ? vscode.ConfigurationTarget.WorkspaceFolder
                : inspected?.workspaceValue !== undefined ? vscode.ConfigurationTarget.Workspace
                    : vscode.ConfigurationTarget.Global;
            await config.update(ENABLED, !enabled(), target);
        }),
        vscode.commands.registerCommand('emojicode.openLearnerDocs', async () => {
            const editor = vscode.window.activeTextEditor;
            if (editor?.document.languageId !== 'emojicode') return;
            const document = editor.document;
            // The selected token, or the one at the cursor, or the one that ends there, e.g. right after typing it.
            let offset = document.offsetAt(editor.selection.start);
            if (offset > 0 && analyse(document).explain(offset) === undefined) offset--;
            const found = await locate(document, offset);
            if (found === undefined) {
                vscode.window.showInformationMessage('The Emojicode documentation does not explain the token at the cursor.');
                return;
            }
            await vscode.env.openExternal(vscode.Uri.parse(found.url));
        }),
        vscode.workspace.onDidCloseTextDocument((document) => documents.delete(document.uri.toString())),
    );

    const status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    status.command = 'emojicode.toggleLearnerDocs';
    const updateStatus = () => {
        const on = enabled();
        status.text = on ? '$(book) Learner docs' : '$(book) Learner docs off';
        status.tooltip = on ? 'Hover over code to find what explains it in the Emojicode documentation. Click to turn off.'
            : 'Click to link code to the Emojicode documentation when you hover over it.';
        if (vscode.window.activeTextEditor?.document.languageId === 'emojicode') status.show(); else status.hide();
    };
    updateStatus();
    context.subscriptions.push(status,
        vscode.window.onDidChangeActiveTextEditor(updateStatus),
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (event.affectsConfiguration(`emojicode.${ENABLED}`)) updateStatus();
        }));
}

function enabled(): boolean {
    return vscode.workspace.getConfiguration('emojicode').get<boolean>(ENABLED, false);
}

function url(path: string): string {
    let base = vscode.workspace.getConfiguration('emojicode').get<string>(BASE_URL, '') || 'https://www.emojicode.org/';
    if (!base.endsWith('/')) base += '/';
    return base + path;
}

function hoverText(section: Section, link: string): vscode.MarkdownString {
    const text = new vscode.MarkdownString();
    text.appendMarkdown('**Learn** · ');
    text.appendText(section.summary);
    text.appendMarkdown(`\n\n[$(book) ${escapeLinkText(section.title)}](${link})`);
    text.supportThemeIcons = true;
    return text;
}

function escapeLinkText(text: string): string {
    return text.replace(/[\\[\]*_`]/g, (c) => `\\${c}`);
}

function range(document: vscode.TextDocument, piece: Piece): vscode.Range {
    return new vscode.Range(document.positionAt(piece.start), document.positionAt(piece.end));
}
