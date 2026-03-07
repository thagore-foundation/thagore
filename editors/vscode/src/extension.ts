import * as vscode from 'vscode';
import { LanguageClient, ServerOptions, TransportKind } from 'vscode-languageclient/node';

export function activate(context: vscode.ExtensionContext) {
  const serverPath = context.asAbsolutePath('../../target/release/thagore-lsp');

  const serverOptions: ServerOptions = {
    run: { command: serverPath, transport: TransportKind.stdio },
    debug: { command: serverPath, args: ['--debug'], transport: TransportKind.stdio }
  };

  const clientOptions = {
    documentSelector: [{ scheme: 'file', language: 'thagore' }],
  };

  const client = new LanguageClient('thagore-lsp', 'Thagore LSP', serverOptions, clientOptions);
  context.subscriptions.push(client.start());
}

export function deactivate(): Thenable<void> | undefined {
  return undefined;
}
