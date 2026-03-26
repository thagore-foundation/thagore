//! Stateful analysis and indexing for Thagore source files.

use std::collections::{BTreeMap, HashMap};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use bumpalo::Bump;
use dashmap::DashMap;
use ropey::Rope;
use thagore_ast::{
    BlockRef, ConstDecl, Decl, FuncDecl, GenericFuncDecl, GenericStructDecl, ImportDecl,
    InternedStr, LetDecl, NodeId, Span, Stmt, TypeExprRef,
};
use thagore_lexer::Lexer;
use thagore_module_graph::{ImportSpec, ModuleResolver};
use thagore_parser::Parser;
use thagore_typeck::{TypeChecker, TypeTable};
use tower_lsp::lsp_types::{CompletionItemKind, Diagnostic, DocumentSymbol, SymbolKind, Url};

use crate::diagnostics::{
    diagnostic_from_parse_error, diagnostic_from_type_error, doc_comment_for_offset,
    line_index_from_offset, span_to_range, type_expr_label,
};

/// Builtin functions available in every file.
pub(crate) const BUILTINS: &[(&str, &str, &str)] = &[
    ("print", "(s: str)", "Writes text to stdout without a trailing newline."),
    ("println", "(s: str)", "Writes text to stdout followed by a newline."),
    ("eprint", "(s: str)", "Writes text to stderr without a trailing newline."),
    ("eprintln", "(s: str)", "Writes text to stderr followed by a newline."),
    ("flush", "()", "Flushes pending stdout output."),
];

/// Symbol categories used by editor features.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum SymbolCategory {
    /// Top-level or local function.
    Function,
    /// Local variable or parameter.
    Variable,
    /// Imported or declared module.
    Module,
    /// Top-level constant.
    Constant,
    /// Nominal struct type.
    Struct,
    /// Builtin function.
    Builtin,
}

impl SymbolCategory {
    /// Converts the symbol category into an LSP completion item kind.
    #[must_use]
    pub(crate) const fn completion_kind(self) -> CompletionItemKind {
        match self {
            Self::Function | Self::Builtin => CompletionItemKind::FUNCTION,
            Self::Variable => CompletionItemKind::VARIABLE,
            Self::Module => CompletionItemKind::MODULE,
            Self::Constant => CompletionItemKind::CONSTANT,
            Self::Struct => CompletionItemKind::STRUCT,
        }
    }

    /// Converts the symbol category into an LSP document symbol kind.
    #[must_use]
    pub(crate) const fn document_kind(self) -> SymbolKind {
        match self {
            Self::Function | Self::Builtin => SymbolKind::FUNCTION,
            Self::Variable => SymbolKind::VARIABLE,
            Self::Module => SymbolKind::MODULE,
            Self::Constant => SymbolKind::CONSTANT,
            Self::Struct => SymbolKind::STRUCT,
        }
    }
}

/// Indexed symbol entry used by completion, hover, goto, and workspace search.
#[derive(Debug, Clone)]
pub(crate) struct IndexedSymbol {
    /// Display label for the symbol.
    pub name: String,
    /// Symbol category.
    pub category: SymbolCategory,
    /// Human-readable signature or declaration summary.
    pub detail: String,
    /// Attached doc comment, if any.
    pub documentation: Option<String>,
    /// Absolute file path defining the symbol.
    pub file_path: PathBuf,
    /// Full source span of the symbol declaration.
    pub span: Span,
    /// Narrower selection span, usually the identifier.
    pub selection_span: Span,
    /// Optional lexical scope span for local bindings.
    pub scope_span: Option<Span>,
    /// Optional module qualifier exposing this symbol.
    pub module_label: Option<String>,
}

/// Imported symbol surface for one file.
#[derive(Debug, Clone)]
pub(crate) struct ImportedModule {
    /// Raw module path as written by the source file.
    pub import_path: String,
    /// Qualifier bound in the local file (`math`, `m`, `utils`, ...).
    pub qualifier: String,
    /// Resolved file path when known.
    pub file_path: Option<PathBuf>,
    /// Span of the import declaration.
    pub span: Span,
    /// `true` when this import contributes all exported names to the unqualified scope.
    pub include_all: bool,
    /// Specific symbols imported directly into scope.
    pub direct_symbols: Vec<IndexedSymbol>,
    /// Export surface available under the module qualifier.
    pub exports: Vec<IndexedSymbol>,
}

/// Successful analysis snapshot for one file.
#[derive(Debug, Clone)]
pub(crate) struct FileAnalysis {
    /// Canonical source text used during analysis.
    pub source: String,
    /// Diagnostics currently active for the file.
    pub diagnostics: Vec<Diagnostic>,
    /// Outline entries for the document.
    pub symbols: Vec<DocumentSymbol>,
    /// Indexed declarations and local symbols.
    pub indexed_symbols: Vec<IndexedSymbol>,
    /// Imported module bindings visible in the file.
    pub imports: Vec<ImportedModule>,
    /// Last successful type table, if type checking succeeded.
    pub type_table: Option<TypeTable>,
}

/// Concurrent analysis host shared across all LSP requests.
#[derive(Debug, Clone)]
pub(crate) struct AnalysisHost {
    inner: Arc<AnalysisHostInner>,
}

#[derive(Debug)]
struct AnalysisHostInner {
    documents: DashMap<Url, Rope>,
    type_cache: DashMap<Url, FileAnalysis>,
    module_cache: DashMap<PathBuf, Vec<IndexedSymbol>>,
    debounce: DashMap<Url, tokio::task::JoinHandle<()>>,
    workspace_root: PathBuf,
    stdlib_root: PathBuf,
    stdlib_exports: HashMap<String, Vec<IndexedSymbol>>,
    debug: bool,
}

impl AnalysisHost {
    /// Creates a new analysis host rooted at `workspace_root`.
    #[must_use]
    pub(crate) fn new(workspace_root: PathBuf, debug: bool) -> Self {
        let stdlib_root = std::env::var_os("THAGORE_STDLIB")
            .map(PathBuf::from)
            .unwrap_or_else(|| workspace_root.join("stdlib"));
        Self::with_paths(workspace_root, stdlib_root, debug)
    }

    /// Creates a new analysis host with explicit roots.
    #[must_use]
    pub(crate) fn with_paths(workspace_root: PathBuf, stdlib_root: PathBuf, debug: bool) -> Self {
        let stdlib_exports = build_stdlib_index(&stdlib_root);
        Self {
            inner: Arc::new(AnalysisHostInner {
                documents: DashMap::new(),
                type_cache: DashMap::new(),
                module_cache: DashMap::new(),
                debounce: DashMap::new(),
                workspace_root,
                stdlib_root,
                stdlib_exports,
                debug,
            }),
        }
    }

    /// Returns the workspace root.
    #[must_use]
    pub(crate) fn workspace_root(&self) -> &Path {
        &self.inner.workspace_root
    }

    /// Returns the standard library root.
    #[must_use]
    pub(crate) fn stdlib_root(&self) -> &Path {
        &self.inner.stdlib_root
    }

    /// Returns the indexed stdlib export map.
    #[must_use]
    pub(crate) fn stdlib_exports(&self) -> &HashMap<String, Vec<IndexedSymbol>> {
        &self.inner.stdlib_exports
    }

    /// Returns the debug flag.
    #[must_use]
    pub(crate) fn debug(&self) -> bool {
        self.inner.debug
    }

    /// Returns the current document text, if tracked.
    #[must_use]
    pub(crate) fn document_text(&self, uri: &Url) -> Option<String> {
        self.inner.documents.get(uri).map(|rope| rope.to_string())
    }

    /// Updates the tracked text buffer for `uri`.
    pub(crate) fn set_document(&self, uri: Url, text: String) {
        self.inner.documents.insert(uri, Rope::from_str(&text));
    }

    /// Applies an updated rope buffer for `uri`.
    pub(crate) fn set_document_rope(&self, uri: Url, rope: Rope) {
        self.inner.documents.insert(uri, rope);
    }

    /// Returns the last successful file analysis, if any.
    #[must_use]
    pub(crate) fn cached_analysis(&self, uri: &Url) -> Option<FileAnalysis> {
        self.inner.type_cache.get(uri).map(|entry| entry.clone())
    }

    /// Stores a file analysis snapshot.
    pub(crate) fn cache_analysis(&self, uri: Url, analysis: FileAnalysis) {
        self.inner.type_cache.insert(uri, analysis);
    }

    /// Removes and aborts an existing debounce task.
    pub(crate) fn cancel_debounce(&self, uri: &Url) {
        if let Some((_, handle)) = self.inner.debounce.remove(uri) {
            handle.abort();
        }
    }

    /// Records a new debounce task.
    pub(crate) fn set_debounce(&self, uri: Url, handle: tokio::task::JoinHandle<()>) {
        self.inner.debounce.insert(uri, handle);
    }

    /// Analyzes the current in-memory contents for `uri`.
    pub(crate) fn analyze_uri(&self, uri: &Url) -> FileAnalysis {
        let path = uri
            .to_file_path()
            .unwrap_or_else(|_| self.workspace_root().join("scratch.tg"));
        let source = self.document_text(uri).unwrap_or_default();
        self.analyze_source(&path, source)
    }

    /// Analyzes the given source contents as if they came from `path`.
    pub(crate) fn analyze_source(&self, path: &Path, source: String) -> FileAnalysis {
        let (diagnostics, symbols, indexed_symbols, imports, type_table) = {
            let arena = Bump::new();
            let mut lexer = Lexer::new(&source);
            let tokens = lexer.lex_all_in(&arena);
            let mut parser = Parser::new(tokens.as_slice(), &arena, lexer.interner());
            let decls = parser.parse_program();
            let parse_errors = parser.take_errors();
            let mut collector = SymbolCollector::new(path, &source, &parser);
            collector.collect(&decls);
            let mut diagnostics = parse_errors
                .iter()
                .map(|error| diagnostic_from_parse_error(&source, error))
                .collect::<Vec<_>>();
            let mut type_table = None;

            if diagnostics.is_empty() {
                let mut checker = TypeChecker::new();
                register_symbol_names(&mut checker, &parser, &decls);
                match checker.check(&decls) {
                    Ok(table) => {
                        type_table = Some(table);
                    }
                    Err(errors) => diagnostics.extend(
                        errors
                            .iter()
                            .map(|error| diagnostic_from_type_error(&source, error, &checker)),
                    ),
                }
            }

            let imports = self.resolve_imports(path, &source, &parser, &decls);
            let mut indexed_symbols = collector.indexed_symbols;
            indexed_symbols.extend(builtin_symbols(path));

            (
                diagnostics,
                collector.document_symbols,
                indexed_symbols,
                imports,
                type_table,
            )
        };

        FileAnalysis {
            source,
            diagnostics,
            symbols,
            indexed_symbols,
            imports,
            type_table,
        }
    }

    /// Returns the export surface for a resolved module file.
    pub(crate) fn exports_for_path(&self, path: &Path) -> Vec<IndexedSymbol> {
        if let Some(entry) = self.inner.module_cache.get(path) {
            return entry.clone();
        }

        let source = match fs::read_to_string(path) {
            Ok(source) => source,
            Err(_) => return Vec::new(),
        };
        let arena = Bump::new();
        let mut lexer = Lexer::new(&source);
        let tokens = lexer.lex_all_in(&arena);
        let mut parser = Parser::new(tokens.as_slice(), &arena, lexer.interner());
        let decls = parser.parse_program();
        let mut collector = SymbolCollector::new(path, &source, &parser);
        collector.collect_public_surface(&decls);
        self.inner
            .module_cache
            .insert(path.to_path_buf(), collector.indexed_symbols.clone());
        collector.indexed_symbols
    }

    /// Lists workspace symbol candidates matching `query`.
    pub(crate) fn workspace_symbols(&self, query: &str) -> Vec<IndexedSymbol> {
        let mut matches = Vec::new();
        collect_workspace_symbols(self.workspace_root(), query, self, &mut matches);
        matches.sort_by(|left, right| left.name.cmp(&right.name));
        matches
    }

    fn resolve_imports<'ast>(
        &self,
        path: &Path,
        source: &str,
        parser: &Parser<'_, '_, 'ast>,
        decls: &'ast [Decl<'ast>],
    ) -> Vec<ImportedModule> {
        let mut resolver = ModuleResolver::new(
            self.stdlib_root().to_path_buf(),
            self.workspace_root().to_path_buf(),
            Vec::new(),
        );
        let mut imports = Vec::new();

        for decl in decls {
            let Decl::Import(import_decl) = decl else {
                continue;
            };

            let import_path = import_decl_path(import_decl, parser);
            let import_spec = ImportSpec {
                path: import_path.clone(),
                alias: import_decl.alias.and_then(|alias| parser.resolve_symbol(alias).map(str::to_string)),
                line: line_index_from_offset(source, import_decl.span.start) + 1,
                allow_parent_fallback: false,
            };
            let resolved = resolver.resolve(&import_spec, path).ok();
            let qualifier = import_decl
                .alias
                .and_then(|alias| parser.resolve_symbol(alias).map(str::to_string))
                .unwrap_or_else(|| default_import_qualifier(import_decl, parser));
            let (file_path, exports) = if let Some(resolved) = resolved {
                let exports = if matches!(resolved.module.source, thagore_module_graph::ModuleSource::Stdlib)
                {
                    self.inner
                        .stdlib_exports
                        .get(&qualifier)
                        .cloned()
                        .or_else(|| self.inner.stdlib_exports.get(&resolved.label).cloned())
                        .unwrap_or_else(|| self.exports_for_path(&resolved.module.file_path))
                } else {
                    self.exports_for_path(&resolved.module.file_path)
                };
                (Some(resolved.module.file_path), exports)
            } else {
                (None, Vec::new())
            };

            let direct_symbols = import_decl
                .symbols
                .iter()
                .filter_map(|symbol| {
                    let import_name = parser.resolve_symbol(symbol.name)?.to_string();
                    let local_name = symbol
                        .alias
                        .and_then(|alias| parser.resolve_symbol(alias).map(str::to_string))
                        .unwrap_or_else(|| import_name.clone());
                    let export = exports
                        .iter()
                        .find(|export| export.name == import_name)
                        .cloned()
                        .unwrap_or_else(|| IndexedSymbol {
                            name: local_name.clone(),
                            category: SymbolCategory::Variable,
                            detail: format!("imported from {}", qualifier),
                            documentation: None,
                            file_path: file_path.clone().unwrap_or_else(|| path.to_path_buf()),
                            span: symbol.span,
                            selection_span: symbol.span,
                            scope_span: None,
                            module_label: Some(qualifier.clone()),
                        });
                    Some(IndexedSymbol {
                        name: local_name,
                        ..export
                    })
                })
                .collect::<Vec<_>>();

            imports.push(ImportedModule {
                import_path,
                qualifier,
                file_path,
                span: import_decl.span,
                include_all: import_decl.include_all,
                direct_symbols,
                exports,
            });
        }

        imports
    }
}

struct SymbolCollector<'src, 'tok, 'ast> {
    path: &'src Path,
    source: &'src str,
    parser: &'src Parser<'src, 'tok, 'ast>,
    document_symbols: Vec<DocumentSymbol>,
    indexed_symbols: Vec<IndexedSymbol>,
}

impl<'src, 'tok, 'ast> SymbolCollector<'src, 'tok, 'ast> {
    fn new(path: &'src Path, source: &'src str, parser: &'src Parser<'src, 'tok, 'ast>) -> Self {
        Self {
            path,
            source,
            parser,
            document_symbols: Vec::new(),
            indexed_symbols: Vec::new(),
        }
    }

    fn collect(&mut self, decls: &'ast [Decl<'ast>]) {
        for decl in decls {
            self.collect_decl(decl, None);
        }
    }

    fn collect_public_surface(&mut self, decls: &'ast [Decl<'ast>]) {
        for decl in decls {
            match decl {
                Decl::Func(_) | Decl::GenericFunc(_) | Decl::Const(_) | Decl::Struct(_) | Decl::GenericStruct(_) => {
                    self.collect_decl(decl, None);
                }
                _ => {}
            }
        }
    }

    fn collect_decl(&mut self, decl: &'ast Decl<'ast>, scope_span: Option<Span>) {
        match decl {
            Decl::Func(func) => self.collect_func(func, scope_span, false),
            Decl::GenericFunc(func) => self.collect_generic_func(func, scope_span),
            Decl::Const(decl) => self.collect_const(decl),
            Decl::Struct(decl) => self.collect_struct(decl.name, decl.id, decl.span, false),
            Decl::GenericStruct(decl) => self.collect_generic_struct(decl),
            Decl::Import(decl) => self.collect_import_decl(decl),
            Decl::Extern(decl) => {
                let name = self.resolve(decl.name);
                self.push_symbol(
                    name,
                    SymbolCategory::Function,
                    format!(
                        "extern func {}{} -> {}",
                        self.resolve(decl.name),
                        format_params(self.parser, decl.params),
                        type_expr_label(self.parser, decl.return_type)
                    ),
                    decl.span,
                    decl.span,
                    scope_span,
                );
            }
            _ => {}
        }
    }

    fn collect_const(&mut self, decl: &'ast ConstDecl<'ast>) {
        let name = self.resolve(decl.name);
        let detail = format!(
            "const {}: {}",
            name,
            type_expr_label(self.parser, decl.type_ann)
        );
        self.document_symbols.push(DocumentSymbol {
            name: name.clone(),
            detail: Some(detail.clone()),
            kind: SymbolKind::CONSTANT,
            tags: None,
            deprecated: None,
            range: span_to_range(self.source, decl.span),
            selection_range: span_to_range(self.source, decl.span),
            children: None,
        });
        self.push_symbol(
            name,
            SymbolCategory::Constant,
            detail,
            decl.span,
            decl.span,
            None,
        );
    }

    fn collect_struct(&mut self, name: InternedStr, id: NodeId, span: Span, generic: bool) {
        let label = self.resolve(name);
        let detail = if generic {
            format!("struct {}<...>", label)
        } else {
            format!("struct {}", label)
        };
        self.document_symbols.push(DocumentSymbol {
            name: label.clone(),
            detail: Some(detail.clone()),
            kind: SymbolKind::STRUCT,
            tags: None,
            deprecated: None,
            range: span_to_range(self.source, span),
            selection_range: span_to_range(self.source, span),
            children: None,
        });
        self.push_symbol(label, SymbolCategory::Struct, detail, span, span, None);
        let _ = id;
    }

    fn collect_generic_struct(&mut self, decl: &'ast GenericStructDecl<'ast>) {
        self.collect_struct(decl.name, decl.id, decl.span, true);
    }

    fn collect_func(&mut self, decl: &'ast FuncDecl<'ast>, scope_span: Option<Span>, generic: bool) {
        let name = self.resolve(decl.name);
        let detail = function_signature(self.parser, name.clone(), decl.params, decl.return_type, generic);
        self.document_symbols.push(DocumentSymbol {
            name: name.clone(),
            detail: Some(detail.clone()),
            kind: SymbolKind::FUNCTION,
            tags: None,
            deprecated: None,
            range: span_to_range(self.source, decl.span),
            selection_range: span_to_range(self.source, decl.span),
            children: None,
        });
        self.push_symbol(
            name.clone(),
            SymbolCategory::Function,
            detail.clone(),
            decl.span,
            decl.span,
            scope_span,
        );

        for param in decl.params {
            let param_name = self.resolve(param.name);
            let param_detail = format!(
                "{}: {}",
                param_name,
                type_expr_label(self.parser, param.ty)
            );
            self.push_symbol(
                param_name,
                SymbolCategory::Variable,
                param_detail,
                param.span,
                param.span,
                Some(decl.body.span),
            );
        }

        self.collect_block(decl.body);
    }

    fn collect_generic_func(&mut self, decl: &'ast GenericFuncDecl<'ast>, scope_span: Option<Span>) {
        let name = self.resolve(decl.name);
        let detail = function_signature(self.parser, name.clone(), decl.params, decl.return_type, true);
        self.document_symbols.push(DocumentSymbol {
            name: name.clone(),
            detail: Some(detail.clone()),
            kind: SymbolKind::FUNCTION,
            tags: None,
            deprecated: None,
            range: span_to_range(self.source, decl.span),
            selection_range: span_to_range(self.source, decl.span),
            children: None,
        });
        self.push_symbol(
            name.clone(),
            SymbolCategory::Function,
            detail,
            decl.span,
            decl.span,
            scope_span,
        );

        for param in decl.params {
            let param_name = self.resolve(param.name);
            let param_detail = format!(
                "{}: {}",
                param_name,
                type_expr_label(self.parser, param.ty)
            );
            self.push_symbol(
                param_name,
                SymbolCategory::Variable,
                param_detail,
                param.span,
                param.span,
                Some(decl.body.span),
            );
        }

        self.collect_block(decl.body);
    }

    fn collect_import_decl(&mut self, decl: &'ast ImportDecl<'ast>) {
        let label = default_import_qualifier(decl, self.parser);
        let detail = format!("import {}", import_decl_path(decl, self.parser));
        self.document_symbols.push(DocumentSymbol {
            name: label.clone(),
            detail: Some(detail.clone()),
            kind: SymbolKind::MODULE,
            tags: None,
            deprecated: None,
            range: span_to_range(self.source, decl.span),
            selection_range: span_to_range(self.source, decl.span),
            children: None,
        });
        self.push_symbol(
            label,
            SymbolCategory::Module,
            detail,
            decl.span,
            decl.span,
            None,
        );
    }

    fn collect_block(&mut self, block: BlockRef<'ast>) {
        for stmt in block.statements {
            self.collect_stmt(stmt, block.span);
        }
    }

    fn collect_stmt(&mut self, stmt: &'ast Stmt<'ast>, scope_span: Span) {
        match stmt {
            Stmt::Let(decl) => self.collect_let(decl, scope_span),
            Stmt::If(node) => {
                self.collect_block(node.then_block);
                if let Some(else_block) = node.else_block {
                    self.collect_block(else_block);
                }
            }
            Stmt::While(node) => self.collect_block(node.body),
            Stmt::For(node) => {
                let binding = self.resolve(node.binding);
                self.push_symbol(
                    binding,
                    SymbolCategory::Variable,
                    "for binding".to_string(),
                    node.span,
                    node.span,
                    Some(node.body.span),
                );
                self.collect_block(node.body);
            }
            _ => {}
        }
    }

    fn collect_let(&mut self, decl: &'ast LetDecl<'ast>, scope_span: Span) {
        let name = self.resolve(decl.name);
        let detail = decl
            .ty
            .map(|ty| format!("let {}: {}", name, type_expr_label(self.parser, ty)))
            .unwrap_or_else(|| format!("let {}", name));
        self.push_symbol(
            name,
            SymbolCategory::Variable,
            detail,
            decl.span,
            decl.span,
            Some(scope_span),
        );
    }

    fn push_symbol(
        &mut self,
        name: String,
        category: SymbolCategory,
        detail: String,
        span: Span,
        selection_span: Span,
        scope_span: Option<Span>,
    ) {
        self.indexed_symbols.push(IndexedSymbol {
            documentation: doc_comment_for_offset(self.source, span.start),
            name,
            category,
            detail,
            file_path: self.path.to_path_buf(),
            span,
            selection_span,
            scope_span,
            module_label: None,
        });
    }

    fn resolve(&self, symbol: InternedStr) -> String {
        self.parser.resolve_symbol(symbol).unwrap_or("__unknown__").to_string()
    }
}

/// Builds a startup stdlib index for completion and hover.
#[must_use]
pub(crate) fn build_stdlib_index(root: &Path) -> HashMap<String, Vec<IndexedSymbol>> {
    let mut modules = HashMap::new();
    collect_stdlib_modules(root, root, &mut modules);
    modules
}

fn collect_stdlib_modules(root: &Path, current: &Path, out: &mut HashMap<String, Vec<IndexedSymbol>>) {
    let Ok(entries) = fs::read_dir(current) else {
        return;
    };

    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            collect_stdlib_modules(root, &path, out);
            continue;
        }
        if path.extension().and_then(|ext| ext.to_str()) != Some("tg") {
            continue;
        }
        let relative = path.strip_prefix(root).unwrap_or(&path);
        let module_name = relative
            .with_extension("")
            .components()
            .map(|component| component.as_os_str().to_string_lossy().into_owned())
            .collect::<Vec<_>>()
            .join(".");
        let source = match fs::read_to_string(&path) {
            Ok(source) => source,
            Err(_) => continue,
        };
        let arena = Bump::new();
        let mut lexer = Lexer::new(&source);
        let tokens = lexer.lex_all_in(&arena);
        let mut parser = Parser::new(tokens.as_slice(), &arena, lexer.interner());
        let decls = parser.parse_program();
        let mut collector = SymbolCollector::new(&path, &source, &parser);
        collector.collect_public_surface(&decls);
        for symbol in &mut collector.indexed_symbols {
            symbol.module_label = Some(module_name.clone());
        }
        out.insert(module_name, collector.indexed_symbols);
    }
}

fn builtin_symbols(path: &Path) -> Vec<IndexedSymbol> {
    BUILTINS
        .iter()
        .map(|(name, params, doc)| IndexedSymbol {
            name: (*name).to_string(),
            category: SymbolCategory::Builtin,
            detail: format!("{name}{params}"),
            documentation: Some((*doc).to_string()),
            file_path: path.to_path_buf(),
            span: Span::empty(),
            selection_span: Span::empty(),
            scope_span: None,
            module_label: None,
        })
        .collect()
}

fn collect_workspace_symbols(
    root: &Path,
    query: &str,
    host: &AnalysisHost,
    out: &mut Vec<IndexedSymbol>,
) {
    let Ok(entries) = fs::read_dir(root) else {
        return;
    };

    for entry in entries.flatten() {
        let path = entry.path();
        if path.file_name().and_then(|name| name.to_str()) == Some("target") {
            continue;
        }
        if path.is_dir() {
            collect_workspace_symbols(&path, query, host, out);
            continue;
        }
        if path.extension().and_then(|ext| ext.to_str()) != Some("tg") {
            continue;
        }
        let exports = host.exports_for_path(&path);
        out.extend(
            exports
                .into_iter()
                .filter(|symbol| symbol.name.contains(query))
                .filter(|symbol| !matches!(symbol.category, SymbolCategory::Variable)),
        );
    }
}

fn register_symbol_names<'ast>(
    checker: &mut TypeChecker,
    parser: &Parser<'_, '_, 'ast>,
    decls: &'ast [Decl<'ast>],
) {
    for decl in decls {
        register_decl_names(checker, parser, decl);
    }
}

fn register_decl_names<'ast>(checker: &mut TypeChecker, parser: &Parser<'_, '_, 'ast>, decl: &'ast Decl<'ast>) {
    match decl {
        Decl::Func(node) => {
            register_name(checker, parser, node.name);
            for param in node.params {
                register_name(checker, parser, param.name);
            }
        }
        Decl::GenericFunc(node) => {
            register_name(checker, parser, node.name);
            for param in node.params {
                register_name(checker, parser, param.name);
            }
        }
        Decl::Let(node) => register_name(checker, parser, node.name),
        Decl::Const(node) => register_name(checker, parser, node.name),
        Decl::Struct(node) => register_name(checker, parser, node.name),
        Decl::GenericStruct(node) => register_name(checker, parser, node.name),
        Decl::Impl(node) => register_name(checker, parser, node.target),
        Decl::GenericImpl(node) => register_name(checker, parser, node.target),
        Decl::Import(node) => {
            if let Some(alias) = node.alias {
                register_name(checker, parser, alias);
            }
            for symbol in node.symbols {
                register_name(checker, parser, symbol.name);
                if let Some(alias) = symbol.alias {
                    register_name(checker, parser, alias);
                }
            }
        }
        Decl::Extern(node) => {
            register_name(checker, parser, node.name);
            for param in node.params {
                register_name(checker, parser, param.name);
            }
        }
        Decl::Intent(node) => register_name(checker, parser, node.name),
        Decl::Flow(node) => register_name(checker, parser, node.name),
    }
}

fn register_name(checker: &mut TypeChecker, parser: &Parser<'_, '_, '_>, symbol: InternedStr) {
    if let Some(name) = parser.resolve_symbol(symbol) {
        checker.register_symbol_name(symbol, name);
    }
}

/// Formats a generic or non-generic function signature from AST data.
#[must_use]
pub(crate) fn function_signature<'ast>(
    parser: &Parser<'_, '_, 'ast>,
    name: String,
    params: &'ast [thagore_ast::Param<'ast>],
    return_type: Option<TypeExprRef<'ast>>,
    generic: bool,
) -> String {
    let suffix = if generic { "<...>" } else { "" };
    let returns = return_type
        .map(|ty| format!(" -> {}", type_expr_label(parser, ty)))
        .unwrap_or_default();
    format!("{name}{suffix}{}{}", format_params(parser, params), returns)
}

/// Formats one parameter list from AST data.
#[must_use]
pub(crate) fn format_params<'ast>(
    parser: &Parser<'_, '_, 'ast>,
    params: &'ast [thagore_ast::Param<'ast>],
) -> String {
    let joined = params
        .iter()
        .map(|param| {
            format!(
                "{}: {}",
                parser.resolve_symbol(param.name).unwrap_or("__unknown__"),
                type_expr_label(parser, param.ty)
            )
        })
        .collect::<Vec<_>>()
        .join(", ");
    format!("({joined})")
}

/// Reconstructs the raw module path string from an import declaration.
#[must_use]
pub(crate) fn import_decl_path<'ast>(
    decl: &'ast ImportDecl<'ast>,
    parser: &Parser<'_, '_, 'ast>,
) -> String {
    let path = decl
        .path_segments
        .iter()
        .filter_map(|segment| parser.resolve_symbol(*segment))
        .collect::<Vec<_>>()
        .join(".");
    if decl.relative_level == 0 {
        return path;
    }
    let prefix = if decl.relative_level == 1 {
        String::from("./")
    } else {
        "../".repeat(usize::from(decl.relative_level.saturating_sub(1)))
    };
    if path.is_empty() {
        prefix.trim_end_matches('/').to_string()
    } else {
        format!("{prefix}{path}")
    }
}

/// Chooses the local qualifier introduced by an import declaration.
#[must_use]
pub(crate) fn default_import_qualifier<'ast>(
    decl: &'ast ImportDecl<'ast>,
    parser: &Parser<'_, '_, 'ast>,
) -> String {
    if let Some(alias) = decl.alias.and_then(|alias| parser.resolve_symbol(alias)) {
        return alias.to_string();
    }
    decl.path_segments
        .last()
        .and_then(|segment| parser.resolve_symbol(*segment))
        .unwrap_or("module")
        .to_string()
}

/// Finds the symbol visible at `offset` within `analysis`.
#[must_use]
pub(crate) fn symbol_at_offset(analysis: &FileAnalysis, offset: u32) -> Option<IndexedSymbol> {
    let mut candidates = analysis
        .indexed_symbols
        .iter()
        .filter(|symbol| {
            if let Some(scope) = symbol.scope_span {
                scope.contains(offset) && symbol.selection_span.start <= offset
            } else {
                symbol.selection_span.contains(offset) || symbol.span.contains(offset)
            }
        })
        .cloned()
        .collect::<Vec<_>>();
    candidates.sort_by_key(|symbol| symbol.selection_span.start);
    candidates.pop()
}

/// Returns direct or imported symbol candidates visible in expression context.
#[must_use]
pub(crate) fn completion_symbols(analysis: &FileAnalysis, offset: u32) -> Vec<IndexedSymbol> {
    let mut by_name = BTreeMap::new();
    for symbol in &analysis.indexed_symbols {
        let visible = symbol
            .scope_span
            .map(|scope| scope.contains(offset) && symbol.selection_span.start <= offset)
            .unwrap_or(true);
        if visible {
            by_name.entry(symbol.name.clone()).or_insert_with(|| symbol.clone());
        }
    }
    for import in &analysis.imports {
        for symbol in &import.direct_symbols {
            by_name
                .entry(symbol.name.clone())
                .or_insert_with(|| symbol.clone());
        }
        if import.include_all {
            for symbol in &import.exports {
                by_name
                    .entry(symbol.name.clone())
                    .or_insert_with(|| symbol.clone());
            }
        }
    }
    by_name.into_values().collect()
}

/// Returns module exports addressable through `qualifier`.
#[must_use]
pub(crate) fn module_exports<'a>(
    analysis: &'a FileAnalysis,
    qualifier: &str,
) -> Option<&'a [IndexedSymbol]> {
    analysis
        .imports
        .iter()
        .find(|import| import.qualifier == qualifier)
        .map(|import| import.exports.as_slice())
}

/// Extracts the identifier and qualifier prefix at a given source offset.
#[must_use]
pub(crate) fn identifier_context(source: &str, offset: usize) -> (Option<String>, String) {
    let bytes = source.as_bytes();
    let clamped = offset.min(bytes.len());
    let mut start = clamped;
    while start > 0 {
        let ch = bytes[start - 1] as char;
        if ch.is_ascii_alphanumeric() || ch == '_' {
            start -= 1;
        } else {
            break;
        }
    }
    let mut end = clamped;
    while end < bytes.len() {
        let ch = bytes[end] as char;
        if ch.is_ascii_alphanumeric() || ch == '_' {
            end += 1;
        } else {
            break;
        }
    }
    let ident = source[start..end.min(source.len())].to_string();

    if start > 0 && bytes[start - 1] == b'.' {
        let qualifier_end = start - 1;
        let mut qualifier_start = qualifier_end;
        while qualifier_start > 0 {
            let ch = bytes[qualifier_start - 1] as char;
            if ch.is_ascii_alphanumeric() || ch == '_' {
                qualifier_start -= 1;
            } else {
                break;
            }
        }
        let qualifier = source[qualifier_start..qualifier_end].to_string();
        return (Some(qualifier), ident);
    }

    (None, ident)
}

/// Suggests the nearest known symbol name by edit distance.
#[must_use]
pub(crate) fn nearest_symbol_name<'a>(
    wanted: &str,
    candidates: impl Iterator<Item = &'a str>,
) -> Option<String> {
    let mut best = None;
    for candidate in candidates {
        let distance = levenshtein(wanted, candidate);
        if distance <= 2 {
            match &best {
                Some((best_distance, _)) if *best_distance <= distance => {}
                _ => best = Some((distance, candidate.to_string())),
            }
        }
    }
    best.map(|(_, name)| name)
}

fn levenshtein(left: &str, right: &str) -> usize {
    let mut prev = (0..=right.chars().count()).collect::<Vec<_>>();
    let mut next = vec![0; prev.len()];
    for (i, left_char) in left.chars().enumerate() {
        next[0] = i + 1;
        for (j, right_char) in right.chars().enumerate() {
            let cost = usize::from(left_char != right_char);
            next[j + 1] = (next[j] + 1).min(prev[j + 1] + 1).min(prev[j] + cost);
        }
        std::mem::swap(&mut prev, &mut next);
    }
    prev[right.chars().count()]
}
