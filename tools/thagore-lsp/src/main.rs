//! Stdio entrypoint for the Thagore language server.

mod analysis;
mod backend;
mod completion;
mod diagnostics;
mod goto;
mod hover;
mod session;
mod symbols;

use std::path::PathBuf;

use tokio::io::{stdin, stdout};
use tower_lsp::{LspService, Server};

use crate::analysis::AnalysisHost;
use crate::backend::Backend;

/// Parses process arguments for the language server.
fn parse_args() -> bool {
    std::env::args().any(|arg| arg == "--debug")
}

/// Resolves the workspace root used by the LSP server.
fn workspace_root() -> PathBuf {
    std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."))
}

#[tokio::main]
async fn main() {
    let debug = parse_args();
    let workspace_root = workspace_root();
    let host = AnalysisHost::new(workspace_root.clone(), debug);
    eprintln!(
        "[thagore-lsp] initialized, stdlib: {}",
        host.stdlib_root().display()
    );

    let (service, socket) = LspService::new(|client| Backend::new(client, host.clone(), debug));
    Server::new(stdin(), stdout(), socket).serve(service).await;
}
