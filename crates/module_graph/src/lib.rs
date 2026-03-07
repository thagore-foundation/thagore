//! Module graph construction for per-module Thagore compilation sessions.
//!
//! This crate owns dependency discovery, topological ordering, and cycle
//! reporting. It deliberately operates on module paths and import surfaces
//! rather than loading full ASTs for the whole program into memory.

pub mod import_table;
pub mod resolver;

use std::collections::{HashMap, HashSet, VecDeque};
use std::fmt;
use std::fs;
use std::path::{Path, PathBuf};

pub use crate::import_table::{
    AmbiguousSymbol, ExportedSymbol, ImportTable, ResolvedSymbol, SymbolKind, TypeSig,
};
pub use crate::resolver::{ModuleResolver, ResolvedModule};

/// Unique module identifier within a single compilation session.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ModuleId(u32);

impl ModuleId {
    /// Creates a new module identifier from its raw numeric value.
    #[must_use]
    pub const fn new(raw: u32) -> Self {
        Self(raw)
    }

    /// Returns the raw numeric identifier.
    #[must_use]
    pub const fn as_u32(self) -> u32 {
        self.0
    }
}

impl fmt::Display for ModuleId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "m{}", self.0)
    }
}

/// Origin of a resolved module.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum ModuleSource {
    /// Module shipped with the compiler standard library.
    Stdlib,
    /// Module resolved from an external dependency cache.
    Dependency {
        /// Package name.
        name: String,
        /// Package version.
        version: String,
    },
    /// Module resolved from the current project root.
    Project,
    /// Module resolved by relative path from the importing file.
    Relative,
}

/// Fully resolved location of a module source file.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct ModulePath {
    /// Resolution source category.
    pub source: ModuleSource,
    /// Absolute path to the `.tg` file.
    pub file_path: PathBuf,
}

impl ModulePath {
    /// Creates a new resolved module path.
    #[must_use]
    pub fn new(source: ModuleSource, file_path: PathBuf) -> Self {
        Self { source, file_path }
    }
}

/// Import specification extracted from source text before full parsing.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImportSpec {
    /// Raw import path as written in source, excluding any alias clause.
    pub path: String,
    /// Optional alias introduced by `as`.
    pub alias: Option<String>,
    /// One-based source line where the import was found.
    pub line: usize,
}

impl ImportSpec {
    /// Returns the path segments separated by `.`.
    #[must_use]
    pub fn segments(&self) -> Vec<&str> {
        self.path
            .split('.')
            .filter(|segment| !segment.is_empty())
            .collect()
    }
}

/// Error produced while building or ordering the module graph.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ModuleError {
    /// The requested entry module does not exist on disk.
    EntryNotFound { path: PathBuf },
    /// Source could not be read.
    Io {
        /// File being read.
        path: PathBuf,
        /// Human-readable error details.
        message: String,
    },
    /// An import declaration is syntactically invalid.
    InvalidImport {
        /// File containing the import.
        file: PathBuf,
        /// One-based source line.
        line: usize,
        /// The original line text.
        text: String,
    },
    /// Resolver failed to resolve an import path.
    ResolveImport {
        /// File containing the import.
        from: PathBuf,
        /// Raw import path.
        import: String,
        /// Human-readable error details.
        message: String,
    },
    /// Dependency graph contains a cycle.
    Cycle(CycleError),
}

impl fmt::Display for ModuleError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::EntryNotFound { path } => {
                write!(f, "entry module not found: {}", path.display())
            }
            Self::Io { path, message } => {
                write!(f, "failed to read {}: {}", path.display(), message)
            }
            Self::InvalidImport { file, line, text } => {
                write!(
                    f,
                    "invalid import in {}:{}: {}",
                    file.display(),
                    line,
                    text.trim()
                )
            }
            Self::ResolveImport {
                from,
                import,
                message,
            } => write!(
                f,
                "failed to resolve import '{}' from {}: {}",
                import,
                from.display(),
                message
            ),
            Self::Cycle(cycle) => write!(f, "{cycle}"),
        }
    }
}

impl std::error::Error for ModuleError {}

/// Cycle detected during topological ordering.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CycleError {
    /// Ordered cycle path, including the repeated start node at the end.
    pub cycle: Vec<ModuleId>,
}

impl CycleError {
    /// Creates a new cycle error.
    #[must_use]
    pub fn new(cycle: Vec<ModuleId>) -> Self {
        Self { cycle }
    }
}

impl fmt::Display for CycleError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("circular import detected: ")?;
        for (index, id) in self.cycle.iter().enumerate() {
            if index > 0 {
                f.write_str(" -> ")?;
            }
            write!(f, "{id}")?;
        }
        Ok(())
    }
}

impl std::error::Error for CycleError {}

/// Resolver abstraction used while constructing the dependency graph.
pub trait ModuleResolve {
    /// Resolves an import declaration relative to the importing file.
    fn resolve_import(
        &mut self,
        import: &ImportSpec,
        from_file: &Path,
    ) -> Result<ModulePath, ModuleError>;
}

/// Directed acyclic graph of modules in a compilation session.
///
/// `edges[id]` stores the direct dependencies imported by `id`.
#[derive(Debug, Clone, Default)]
pub struct ModuleGraph {
    /// Module id to resolved path mapping.
    pub nodes: HashMap<ModuleId, ModulePath>,
    /// Direct dependency edges from importer to imported modules.
    pub edges: HashMap<ModuleId, Vec<ModuleId>>,
    /// Topological order with dependencies before importers.
    pub topo_order: Vec<ModuleId>,
}

impl ModuleGraph {
    /// Builds a complete module graph reachable from `entry`.
    pub fn build<R>(entry: &Path, resolver: &mut R) -> Result<Self, ModuleError>
    where
        R: ModuleResolve,
    {
        let entry_path = canonicalize_existing(entry)?;
        let entry_module = ModulePath::new(ModuleSource::Project, entry_path.clone());

        let mut graph = Self::default();
        let mut next_id = 0_u32;
        let mut by_path: HashMap<PathBuf, ModuleId> = HashMap::new();
        let mut pending: VecDeque<ModuleId> = VecDeque::new();

        let entry_id = ModuleId::new(next_id);
        next_id += 1;
        by_path.insert(entry_path.clone(), entry_id);
        graph.nodes.insert(entry_id, entry_module);
        graph.edges.insert(entry_id, Vec::new());
        pending.push_back(entry_id);

        while let Some(module_id) = pending.pop_front() {
            let module_path = graph
                .nodes
                .get(&module_id)
                .cloned()
                .expect("module id queued before insertion");
            let source = read_source(&module_path.file_path)?;
            let imports = parse_imports(&source, &module_path.file_path)?;
            let mut discovered_edges = Vec::new();

            for import in imports {
                let resolved = resolver.resolve_import(&import, &module_path.file_path)?;
                let canonical = canonicalize_existing(&resolved.file_path)?;
                let dep_id = if let Some(existing) = by_path.get(&canonical).copied() {
                    existing
                } else {
                    let created = ModuleId::new(next_id);
                    next_id += 1;
                    by_path.insert(canonical.clone(), created);
                    graph
                        .nodes
                        .insert(created, ModulePath::new(resolved.source, canonical.clone()));
                    graph.edges.insert(created, Vec::new());
                    pending.push_back(created);
                    created
                };

                if !discovered_edges.contains(&dep_id) {
                    discovered_edges.push(dep_id);
                }
            }

            graph.edges.insert(module_id, discovered_edges);
        }

        graph.topo_sort().map_err(ModuleError::Cycle)?;
        Ok(graph)
    }

    /// Computes dependency-first topological order using Kahn's algorithm.
    pub fn topo_sort(&mut self) -> Result<(), CycleError> {
        let mut indegree: HashMap<ModuleId, usize> =
            self.nodes.keys().copied().map(|id| (id, 0_usize)).collect();
        let mut reverse_edges: HashMap<ModuleId, Vec<ModuleId>> = HashMap::new();

        for (module_id, deps) in &self.edges {
            indegree.insert(*module_id, deps.len());
            for dep in deps {
                reverse_edges.entry(*dep).or_default().push(*module_id);
            }
        }

        let mut ready: VecDeque<ModuleId> = indegree
            .iter()
            .filter_map(|(id, degree)| (*degree == 0).then_some(*id))
            .collect();
        let mut order = Vec::with_capacity(self.nodes.len());

        while let Some(module_id) = ready.pop_front() {
            order.push(module_id);
            if let Some(importers) = reverse_edges.get(&module_id) {
                for importer in importers {
                    if let Some(degree) = indegree.get_mut(importer) {
                        *degree = degree.saturating_sub(1);
                        if *degree == 0 {
                            ready.push_back(*importer);
                        }
                    }
                }
            }
        }

        if order.len() != self.nodes.len() {
            return Err(CycleError::new(self.detect_cycles().unwrap_or_default()));
        }

        self.topo_order = order;
        Ok(())
    }

    /// Returns one cycle path when the graph is cyclic.
    #[must_use]
    pub fn detect_cycles(&self) -> Option<Vec<ModuleId>> {
        #[derive(Clone, Copy, PartialEq, Eq)]
        enum VisitState {
            Visiting,
            Visited,
        }

        fn dfs(
            current: ModuleId,
            graph: &ModuleGraph,
            states: &mut HashMap<ModuleId, VisitState>,
            stack: &mut Vec<ModuleId>,
            stack_set: &mut HashSet<ModuleId>,
        ) -> Option<Vec<ModuleId>> {
            states.insert(current, VisitState::Visiting);
            stack.push(current);
            stack_set.insert(current);

            if let Some(deps) = graph.edges.get(&current) {
                for dep in deps {
                    if stack_set.contains(dep) {
                        let start = stack.iter().position(|id| id == dep).unwrap_or(0);
                        let mut cycle = stack[start..].to_vec();
                        cycle.push(*dep);
                        return Some(cycle);
                    }

                    if !matches!(states.get(dep), Some(VisitState::Visited)) {
                        if let Some(cycle) = dfs(*dep, graph, states, stack, stack_set) {
                            return Some(cycle);
                        }
                    }
                }
            }

            stack.pop();
            stack_set.remove(&current);
            states.insert(current, VisitState::Visited);
            None
        }

        let mut states = HashMap::new();
        let mut stack = Vec::new();
        let mut stack_set = HashSet::new();

        for module_id in self.nodes.keys().copied() {
            if !states.contains_key(&module_id) {
                if let Some(cycle) = dfs(module_id, self, &mut states, &mut stack, &mut stack_set) {
                    return Some(cycle);
                }
            }
        }

        None
    }
}

fn canonicalize_existing(path: &Path) -> Result<PathBuf, ModuleError> {
    if !path.exists() {
        return Err(ModuleError::EntryNotFound {
            path: path.to_path_buf(),
        });
    }

    path.canonicalize().map_err(|error| ModuleError::Io {
        path: path.to_path_buf(),
        message: error.to_string(),
    })
}

fn read_source(path: &Path) -> Result<String, ModuleError> {
    fs::read_to_string(path).map_err(|error| ModuleError::Io {
        path: path.to_path_buf(),
        message: error.to_string(),
    })
}

fn parse_imports(source: &str, file: &Path) -> Result<Vec<ImportSpec>, ModuleError> {
    let mut imports = Vec::new();

    for (index, line) in source.lines().enumerate() {
        let line_number = index + 1;
        let trimmed = line.trim();

        if trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }

        if trimmed.starts_with("import ") {
            let body = trimmed.trim_start_matches("import ").trim();
            if body.is_empty() {
                return Err(ModuleError::InvalidImport {
                    file: file.to_path_buf(),
                    line: line_number,
                    text: line.to_string(),
                });
            }

            let mut alias = None;
            let mut parts = body.split_whitespace();
            let path_part = parts.next().unwrap_or_default();
            let remaining: Vec<&str> = parts.collect();

            if remaining.len() == 2 && remaining[0] == "as" {
                alias = Some(remaining[1].to_string());
            } else if remaining.len() == 2 && remaining[0] == "include" && remaining[1] == "all" {
            } else if remaining.len() == 4
                && remaining[0] == "as"
                && remaining[2] == "include"
                && remaining[3] == "all"
            {
                alias = Some(remaining[1].to_string());
            } else if !remaining.is_empty() {
                return Err(ModuleError::InvalidImport {
                    file: file.to_path_buf(),
                    line: line_number,
                    text: line.to_string(),
                });
            }

            if path_part.is_empty() {
                return Err(ModuleError::InvalidImport {
                    file: file.to_path_buf(),
                    line: line_number,
                    text: line.to_string(),
                });
            }

            imports.push(ImportSpec {
                path: path_part.to_string(),
                alias,
                line: line_number,
            });
            continue;
        }

        if trimmed.starts_with("from ") {
            let Some((module_path, import_tail)) = trimmed
                .trim_start_matches("from ")
                .trim()
                .split_once(" import ")
            else {
                return Err(ModuleError::InvalidImport {
                    file: file.to_path_buf(),
                    line: line_number,
                    text: line.to_string(),
                });
            };
            if module_path.trim().is_empty() {
                return Err(ModuleError::InvalidImport {
                    file: file.to_path_buf(),
                    line: line_number,
                    text: line.to_string(),
                });
            }
            let module_path = module_path.trim();
            let path = if module_path == "." || module_path == ".." {
                let symbol = import_tail
                    .split(',')
                    .next()
                    .unwrap_or_default()
                    .split_whitespace()
                    .next()
                    .unwrap_or_default()
                    .trim();
                if symbol.is_empty() {
                    module_path.to_string()
                } else if module_path == "." {
                    format!("./{symbol}")
                } else {
                    format!("../{symbol}")
                }
            } else {
                module_path.to_string()
            };
            imports.push(ImportSpec {
                path,
                alias: None,
                line: line_number,
            });
            continue;
        }
    }

    Ok(imports)
}

#[cfg(test)]
mod tests {
    use super::{ModuleGraph, ModuleId, ModulePath, ModuleSource};
    use std::collections::HashMap;
    use std::path::PathBuf;

    #[test]
    fn topo_sort_orders_dependencies_before_importers() {
        let a = ModuleId::new(0);
        let b = ModuleId::new(1);
        let c = ModuleId::new(2);

        let mut graph = ModuleGraph {
            nodes: HashMap::from([
                (
                    a,
                    ModulePath::new(ModuleSource::Project, PathBuf::from("/a.tg")),
                ),
                (
                    b,
                    ModulePath::new(ModuleSource::Project, PathBuf::from("/b.tg")),
                ),
                (
                    c,
                    ModulePath::new(ModuleSource::Project, PathBuf::from("/c.tg")),
                ),
            ]),
            edges: HashMap::from([(a, vec![b, c]), (b, vec![c]), (c, vec![])]),
            topo_order: Vec::new(),
        };

        graph.topo_sort().unwrap();
        let order = graph.topo_order;

        let position = |id| order.iter().position(|candidate| *candidate == id).unwrap();
        assert!(position(c) < position(b));
        assert!(position(b) < position(a));
    }

    #[test]
    fn detect_cycles_returns_cycle_path() {
        let a = ModuleId::new(0);
        let b = ModuleId::new(1);
        let c = ModuleId::new(2);

        let graph = ModuleGraph {
            nodes: HashMap::from([
                (
                    a,
                    ModulePath::new(ModuleSource::Project, PathBuf::from("/a.tg")),
                ),
                (
                    b,
                    ModulePath::new(ModuleSource::Project, PathBuf::from("/b.tg")),
                ),
                (
                    c,
                    ModulePath::new(ModuleSource::Project, PathBuf::from("/c.tg")),
                ),
            ]),
            edges: HashMap::from([(a, vec![b]), (b, vec![c]), (c, vec![a])]),
            topo_order: Vec::new(),
        };

        let cycle = graph.detect_cycles().unwrap();
        assert!(cycle.len() >= 4);
        assert_eq!(cycle.first(), cycle.last());
        assert!(cycle.contains(&a));
        assert!(cycle.contains(&b));
        assert!(cycle.contains(&c));
    }

    #[test]
    fn topo_sort_reports_cycle_error() {
        let a = ModuleId::new(0);
        let b = ModuleId::new(1);

        let mut graph = ModuleGraph {
            nodes: HashMap::from([
                (
                    a,
                    ModulePath::new(ModuleSource::Project, PathBuf::from("/a.tg")),
                ),
                (
                    b,
                    ModulePath::new(ModuleSource::Project, PathBuf::from("/b.tg")),
                ),
            ]),
            edges: HashMap::from([(a, vec![b]), (b, vec![a])]),
            topo_order: Vec::new(),
        };

        let error = graph.topo_sort().unwrap_err();
        assert_eq!(error.cycle.first(), error.cycle.last());
        assert!(error.cycle.contains(&a));
        assert!(error.cycle.contains(&b));
    }
}
