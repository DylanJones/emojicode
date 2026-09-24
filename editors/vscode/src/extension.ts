import * as vscode from 'vscode';
import { LanguageClient, LanguageClientOptions, ServerOptions } from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export async function activate(context: vscode.ExtensionContext) {
    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(async (event) => {
        if (event.affectsConfiguration('emojicode.server') || event.affectsConfiguration('emojicode.packageSearchPaths')) {
            await stopClient();
            await startClient();
        }
    }));
    await startClient();
}

export async function deactivate() {
    await stopClient();
}

async function startClient() {
    const config = vscode.workspace.getConfiguration('emojicode');
    const serverOptions: ServerOptions = { command: config.get<string>('server.path', 'emojicode-lsp') };
    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ language: 'emojicode' }],
        initializationOptions: { packageSearchPaths: config.get<string[]>('packageSearchPaths', []) },
    };
    client = new LanguageClient('emojicode', 'Emojicode', serverOptions, clientOptions);
    try {
        await client.start();
    }
    catch (error) {
        client = undefined;
        // Highlighting works without the server, so this is only a warning.
        vscode.window.showWarningMessage(`Could not start the Emojicode language server (${error}). ` +
            'Set emojicode.server.path to the emojicode-lsp executable to get errors, hover and completion.');
    }
}

async function stopClient() {
    if (client) {
        await client.stop();
        client = undefined;
    }
}
