//! Cross-module export registry used by session-based compilation.

use std::collections::HashMap;

use crate::ModuleId;

/// Export category exposed from one compiled module.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SymbolKind {
    /// Callable function symbol.
    Function,
    /// Nominal struct type symbol.
    Struct,
    /// Top-level constant symbol.
    Const,
    /// Type-only export.
    Type,
}

/// Textual type information preserved across module boundaries.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TypeSig {
    /// Function signature `(params...) -> return`.
    Function {
        /// Parameter type names in declaration order.
        params: Vec<String>,
        /// Return type name.
        return_type: String,
    },
    /// Nominal struct name.
    Struct { name: String },
    /// Constant value type.
    Const { ty: String },
    /// Type alias or other nominal export.
    Named(String),
}

/// One exported symbol from a module.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExportedSymbol {
    /// Unqualified symbol name as imported by users.
    pub name: String,
    /// Concrete linker-visible name emitted for this symbol.
    pub qualified_name: String,
    /// Optional alias defined by the exporting module.
    pub alias: Option<String>,
    /// Export category.
    pub kind: SymbolKind,
    /// Cross-module type information.
    pub type_sig: TypeSig,
}

/// Resolved symbol selected for an import lookup.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResolvedSymbol {
    /// Owning module id.
    pub module_id: ModuleId,
    /// Chosen exported symbol.
    pub symbol: ExportedSymbol,
}

/// Conflict produced when `include all` exposes the same symbol from multiple modules.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AmbiguousSymbol {
    /// The conflicting symbol name.
    pub name: String,
    /// Candidate module ids that export this name.
    pub modules: Vec<ModuleId>,
}

/// Lookup table of exported module symbols accumulated during a session.
#[derive(Debug, Clone, Default)]
pub struct ImportTable {
    /// Export list per module.
    pub exports: HashMap<ModuleId, Vec<ExportedSymbol>>,
    /// `include all` conflict tracking by unqualified symbol name.
    pub global_symbols: HashMap<String, Vec<ModuleId>>,
    /// Human-readable module label to module id mapping.
    pub module_labels: HashMap<String, ModuleId>,
}

impl ImportTable {
    /// Creates an empty import table.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Registers the export surface for one module.
    pub fn register_module_exports(
        &mut self,
        id: ModuleId,
        module_label: String,
        exports: Vec<ExportedSymbol>,
    ) {
        self.module_labels.insert(module_label, id);
        for export in &exports {
            self.global_symbols
                .entry(export.name.clone())
                .or_default()
                .push(id);
        }
        self.exports.insert(id, exports);
    }

    /// Resolves `name` from a direct import of `module_label`.
    pub fn lookup_qualified(&self, module_label: &str, name: &str) -> Option<&ExportedSymbol> {
        let module_id = self.module_labels.get(module_label)?;
        self.exports
            .get(module_id)?
            .iter()
            .find(|symbol| symbol.name == name || symbol.alias.as_deref() == Some(name))
    }

    /// Resolves `name` from an `include all` surface.
    pub fn resolve_global(&self, name: &str) -> Result<Option<ResolvedSymbol>, AmbiguousSymbol> {
        let Some(modules) = self.global_symbols.get(name) else {
            return Ok(None);
        };
        if modules.len() > 1 {
            return Err(AmbiguousSymbol {
                name: name.to_string(),
                modules: modules.clone(),
            });
        }

        let module_id = modules[0];
        let Some(symbol) = self
            .exports
            .get(&module_id)
            .and_then(|exports| exports.iter().find(|symbol| symbol.name == name))
        else {
            return Ok(None);
        };
        Ok(Some(ResolvedSymbol {
            module_id,
            symbol: symbol.clone(),
        }))
    }

    /// Returns all exports currently known for `module_id`.
    #[must_use]
    pub fn module_exports(&self, module_id: ModuleId) -> &[ExportedSymbol] {
        self.exports
            .get(&module_id)
            .map(Vec::as_slice)
            .unwrap_or(&[])
    }

    /// Returns all module labels currently registered for `name`.
    #[must_use]
    pub fn conflict_labels(&self, name: &str) -> Vec<String> {
        let Some(module_ids) = self.global_symbols.get(name) else {
            return Vec::new();
        };

        let mut labels = self
            .module_labels
            .iter()
            .filter_map(|(label, module_id)| module_ids.contains(module_id).then_some(label.clone()))
            .collect::<Vec<_>>();
        labels.sort();
        labels
    }
}
