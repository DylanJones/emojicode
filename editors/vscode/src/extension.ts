import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { LanguageClient, LanguageClientOptions, ServerOptions } from 'vscode-languageclient/node';
import { registerAutoClose } from './autoClose';
import { registerLearnerDocs } from './learnerHover';

let client: LanguageClient | undefined;
/** The start of the client, while it is in progress. */
let starting: Promise<void> | undefined;

export async function activate(context: vscode.ExtensionContext) {
    registerLearnerDocs(context, () => client);
    registerAutoClose(context);
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

/** Returns whether `command` names an executable file, either as a path or on the PATH. */
function isExecutable(command: string): boolean {
    const candidates = command.includes(path.sep)
        ? [command]
        : (process.env.PATH ?? '').split(path.delimiter).filter((dir) => dir).map((dir) => path.join(dir, command));
    return candidates.some((candidate) => {
        try {
            fs.accessSync(candidate, fs.constants.X_OK);
            return fs.statSync(candidate).isFile();
        }
        catch {
            return false;
        }
    });
}

async function startClient() {
    const config = vscode.workspace.getConfiguration('emojicode');
    const command = config.get<string>('server.path', 'emojicode-lsp');
    if (!isExecutable(command)) {
        // Highlighting works without the server, so this is only a warning.
        vscode.window.showWarningMessage(`The Emojicode language server ${command} was not found. ` +
            'Set emojicode.server.path to the emojicode-lsp executable to get errors, hover and completion.');
        return;
    }
    const serverOptions: ServerOptions = { command };
    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ language: 'emojicode' }],
        initializationOptions: { packageSearchPaths: config.get<string[]>('packageSearchPaths', []) },
    };
    const newClient = new LanguageClient('emojicode', 'Emojicode', serverOptions, clientOptions);
    client = newClient;
    starting = newClient.start();
    try {
        await starting;
    }
    catch {
        // The client already told the user why it could not start. Disposing it removes its output channel.
        if (client === newClient) {
            client = undefined;
        }
        await newClient.dispose().catch(() => undefined);
    }
    finally {
        starting = undefined;
    }
}

async function stopClient() {
    // A client can only be stopped once it has started.
    await starting?.catch(() => undefined);
    const running = client;
    client = undefined;
    await running?.dispose().catch(() => undefined);
}
