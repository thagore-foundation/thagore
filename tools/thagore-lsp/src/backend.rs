//! LSP backend implementation for Thagore.

use tower_lsp::jsonrpc::Result;
use tower_lsp::lsp_types::{
    CompletionOptions, CompletionResponse, DidChangeTextDocumentParams, DidOpenTextDocumentParams,
    DidSaveTextDocumentParams, DocumentSymbolResponse, GotoDefinitionResponse, Hover,
    HoverProviderCapability, InitializeParams, InitializeResult, InitializedParams, MessageType,
    OneOf, ServerCapabilities, ServerInfo, TextDocumentSyncCapability, TextDocumentSyncKind,
};
use tower_lsp::{async_trait, Client, LanguageServer};

use crate::analysis::AnalysisHost;
use crate::completion::completions;
use crate::goto::goto_definition;
use crate::hover::hover;
use crate::session::{apply_changes, handle_save, run_immediate_check, schedule_recheck};
use crate::symbols::{document_symbols, workspace_symbols};

/// Tower-LSP backend serving Thagore analysis features.
#[derive(Debug, Clone)]
pub(crate) struct Backend {
    client: Client,
    host: AnalysisHost,
    debug: bool,
}

impl Backend {
    /// Creates a new backend for one LSP client connection.
    #[must_use]
    pub(crate) fn new(client: Client, host: AnalysisHost, debug: bool) -> Self {
        Self { client, host, debug }
    }
}

#[async_trait]
impl LanguageServer for Backend {
    async fn initialize(&self, params: InitializeParams) -> Result<InitializeResult> {
        if self.debug {
            eprintln!("[thagore-lsp] initialize: {:?}", params.root_uri);
        }
        Ok(InitializeResult {
            server_info: Some(ServerInfo {
                name: "thagore-lsp".to_string(),
                version: Some("0.1.0".to_string()),
            }),
            capabilities: ServerCapabilities {
                text_document_sync: Some(TextDocumentSyncCapability::Kind(
                    TextDocumentSyncKind::INCREMENTAL,
                )),
                completion_provider: Some(CompletionOptions {
                    trigger_characters: Some(vec![".".to_string()]),
                    ..CompletionOptions::default()
                }),
                hover_provider: Some(HoverProviderCapability::Simple(true)),
                definition_provider: Some(OneOf::Left(true)),
                document_symbol_provider: Some(OneOf::Left(true)),
                workspace_symbol_provider: Some(OneOf::Left(true)),
                ..ServerCapabilities::default()
            },
        })
    }

    async fn initialized(&self, _: InitializedParams) {
        self.client
            .log_message(MessageType::INFO, "thagore-lsp ready")
            .await;
    }

    async fn shutdown(&self) -> Result<()> {
        Ok(())
    }

    async fn did_open(&self, params: DidOpenTextDocumentParams) {
        let uri = params.text_document.uri;
        self.host.set_document(uri.clone(), params.text_document.text);
        run_immediate_check(self.host.clone(), self.client.clone(), uri).await;
    }

    async fn did_change(&self, params: DidChangeTextDocumentParams) {
        if self.debug {
            eprintln!(
                "[thagore-lsp] didChange: {} edits={}",
                params.text_document.uri,
                params.content_changes.len()
            );
        }
        let uri = params.text_document.uri.clone();
        apply_changes(&self.host, params);
        schedule_recheck(self.host.clone(), self.client.clone(), uri);
    }

    async fn did_save(&self, params: DidSaveTextDocumentParams) {
        handle_save(self.host.clone(), self.client.clone(), params).await;
    }

    async fn completion(
        &self,
        params: tower_lsp::lsp_types::CompletionParams,
    ) -> Result<Option<CompletionResponse>> {
        Ok(completions(
            &self.host,
            &params.text_document_position.text_document.uri,
            params.text_document_position.position,
        ))
    }

    async fn hover(
        &self,
        params: tower_lsp::lsp_types::HoverParams,
    ) -> Result<Option<Hover>> {
        Ok(hover(
            &self.host,
            &params.text_document_position_params.text_document.uri,
            params.text_document_position_params.position,
        ))
    }

    async fn goto_definition(
        &self,
        params: tower_lsp::lsp_types::GotoDefinitionParams,
    ) -> Result<Option<GotoDefinitionResponse>> {
        Ok(goto_definition(
            &self.host,
            &params.text_document_position_params.text_document.uri,
            params.text_document_position_params.position,
        ))
    }

    async fn document_symbol(
        &self,
        params: tower_lsp::lsp_types::DocumentSymbolParams,
    ) -> Result<Option<DocumentSymbolResponse>> {
        Ok(document_symbols(&self.host, &params.text_document.uri).map(DocumentSymbolResponse::Nested))
    }

    async fn symbol(
        &self,
        params: tower_lsp::lsp_types::WorkspaceSymbolParams,
    ) -> Result<Option<Vec<tower_lsp::lsp_types::SymbolInformation>>> {
        Ok(Some(workspace_symbols(&self.host, &params.query)))
    }
}
