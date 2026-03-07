//! Module path resolution for session-based Thagore compilation.
//!
//! The resolver is deliberately filesystem-oriented. It only maps an import
//! surface to a concrete source file and categorizes where that file came from.
//! It does not parse full modules or load ASTs.

use std::collections::{BTreeSet, HashMap};
use std::fs;
use std::path::{Path, PathBuf};

use crate::{ImportSpec, ModuleError, ModulePath, ModuleResolve, ModuleSource};

/// Fully resolved import target with cached display metadata.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResolvedModule {
    /// Canonical module path and origin classification.
    pub module: ModulePath,
    /// Printable module label derived from the import surface.
    pub label: String,
}

/// Deterministic module resolver used while constructing a compilation session.
#[derive(Debug, Clone)]
pub struct ModuleResolver {
    /// Root directory containing the bundled standard library modules.
    pub stdlib_root: PathBuf,
    /// Root directory of the current project.
    pub project_root: PathBuf,
    /// Extra include roots, typically dependency source directories.
    pub include_dirs: Vec<PathBuf>,
    /// Memoized resolution results keyed by importer path and raw import path.
    pub cache: HashMap<String, ResolvedModule>,
    dependency_versions: HashMap<String, String>,
}

impl ModuleResolver {
    /// Creates a resolver rooted at `project_root` and `stdlib_root`.
    #[must_use]
    pub fn new(stdlib_root: PathBuf, project_root: PathBuf, include_dirs: Vec<PathBuf>) -> Self {
        let dependency_versions = load_dependency_versions(&project_root);
        Self {
            stdlib_root,
            project_root,
            include_dirs,
            cache: HashMap::new(),
            dependency_versions,
        }
    }

    /// Resolves `import` relative to `from_file`, using the configured search order.
    pub fn resolve(
        &mut self,
        import: &ImportSpec,
        from_file: &Path,
    ) -> Result<ResolvedModule, ModuleError> {
        let cache_key = cache_key(from_file, import);
        if let Some(cached) = self.cache.get(&cache_key) {
            return Ok(cached.clone());
        }

        let resolved = self
            .resolve_uncached(import, from_file)
            .map_err(|message| ModuleError::ResolveImport {
                from: from_file.to_path_buf(),
                import: import.path.clone(),
                message,
            })?;
        self.cache.insert(cache_key, resolved.clone());
        Ok(resolved)
    }

    fn resolve_uncached(
        &self,
        import: &ImportSpec,
        from_file: &Path,
    ) -> Result<ResolvedModule, String> {
        if is_relative_import(&import.path) {
            return self.resolve_relative(&import.path, from_file);
        }

        let segments = import.segments();
        if segments.is_empty() {
            return Err(String::from("empty import path"));
        }

        if segments.len() == 1 {
            return self.resolve_single_name(segments[0]);
        }

        if segments.first() == Some(&"std") {
            return self
                .resolve_from_root(&self.stdlib_root, &segments[1..], ModuleSource::Stdlib)
                .map(|module| ResolvedModule {
                    module,
                    label: import.path.clone(),
                })
                .ok_or_else(|| self.not_found_message(&import.path));
        }

        self.resolve_project_path(&segments)
            .map(|module| ResolvedModule {
                module,
                label: import.path.clone(),
            })
            .ok_or_else(|| self.not_found_message(&import.path))
    }

    fn resolve_relative(&self, rel: &str, from_file: &Path) -> Result<ResolvedModule, String> {
        let importer_dir = from_file
            .parent()
            .ok_or_else(|| format!("cannot resolve relative import `{rel}` from root path"))?;
        let (parent_levels, tail_segments) = split_relative_segments(rel);
        let mut base = importer_dir.to_path_buf();
        for _ in 0..parent_levels {
            base = base
                .parent()
                .ok_or_else(|| format!("relative import `{rel}` escapes the project root"))?
                .to_path_buf();
        }

        find_module_candidates(&base, &tail_segments)
            .map(|path| ResolvedModule {
                module: ModulePath::new(ModuleSource::Relative, path),
                label: rel.to_string(),
            })
            .ok_or_else(|| self.not_found_message(rel))
    }

    fn resolve_single_name(&self, name: &str) -> Result<ResolvedModule, String> {
        if let Some(module) =
            self.resolve_from_root(&self.stdlib_root, &[name], ModuleSource::Stdlib)
        {
            return Ok(ResolvedModule {
                module,
                label: name.to_string(),
            });
        }

        if let Some(module) = self.resolve_dependency(name) {
            return Ok(ResolvedModule {
                module,
                label: name.to_string(),
            });
        }

        if let Some(module) = self.resolve_project_path(&[name]) {
            return Ok(ResolvedModule {
                module,
                label: name.to_string(),
            });
        }

        Err(self.not_found_message(name))
    }

    fn resolve_dependency(&self, name: &str) -> Option<ModulePath> {
        for root in &self.include_dirs {
            if let Some(path) = find_module_candidates(root, &[name]) {
                return Some(ModulePath::new(
                    ModuleSource::Dependency {
                        name: name.to_string(),
                        version: self
                            .dependency_versions
                            .get(name)
                            .cloned()
                            .unwrap_or_else(|| String::from("unknown")),
                    },
                    path,
                ));
            }

            let src_root = root.join("src");
            if let Some(path) = find_module_candidates(&src_root, &[name]) {
                return Some(ModulePath::new(
                    ModuleSource::Dependency {
                        name: name.to_string(),
                        version: self
                            .dependency_versions
                            .get(name)
                            .cloned()
                            .unwrap_or_else(|| String::from("unknown")),
                    },
                    path,
                ));
            }
        }

        None
    }

    fn resolve_project_path(&self, segments: &[&str]) -> Option<ModulePath> {
        if segments.is_empty() {
            return None;
        }

        if segments.first() == Some(&"src") {
            return self.resolve_from_root(&self.project_root, segments, ModuleSource::Project);
        }

        let src_root = self.project_root.join("src");
        self.resolve_from_root(&src_root, segments, ModuleSource::Project)
            .or_else(|| self.resolve_from_root(&self.project_root, segments, ModuleSource::Project))
    }

    fn resolve_from_root(
        &self,
        root: &Path,
        segments: &[&str],
        source: ModuleSource,
    ) -> Option<ModulePath> {
        find_module_candidates(root, segments).map(|path| ModulePath::new(source, path))
    }

    fn not_found_message(&self, import_path: &str) -> String {
        let suggestions = self.suggest_modules(import_path);
        if suggestions.is_empty() {
            format!("module `{import_path}` not found")
        } else {
            format!(
                "module `{import_path}` not found; did you mean {}?",
                suggestions.join(", ")
            )
        }
    }

    fn suggest_modules(&self, import_path: &str) -> Vec<String> {
        let wanted = import_path
            .split('.')
            .next_back()
            .unwrap_or(import_path)
            .to_ascii_lowercase();
        let mut candidates = BTreeSet::new();

        collect_module_names(&self.stdlib_root, String::new(), &mut candidates);
        collect_module_names(
            &self.project_root.join("src"),
            String::new(),
            &mut candidates,
        );
        for include_dir in &self.include_dirs {
            collect_module_names(include_dir, String::new(), &mut candidates);
            collect_module_names(&include_dir.join("src"), String::new(), &mut candidates);
        }

        let mut ranked = candidates
            .into_iter()
            .filter_map(|candidate| {
                let leaf = candidate
                    .split('.')
                    .next_back()
                    .unwrap_or(candidate.as_str())
                    .to_ascii_lowercase();
                let score = similarity_score(&wanted, &leaf);
                (score >= 2).then_some((score, candidate))
            })
            .collect::<Vec<_>>();
        ranked.sort_by(|left, right| right.cmp(left));
        ranked.into_iter().take(3).map(|(_, name)| name).collect()
    }
}

impl ModuleResolve for ModuleResolver {
    fn resolve_import(
        &mut self,
        import: &ImportSpec,
        from_file: &Path,
    ) -> Result<ModulePath, ModuleError> {
        self.resolve(import, from_file)
            .map(|resolved| resolved.module)
    }
}

fn cache_key(from_file: &Path, import: &ImportSpec) -> String {
    format!("{}::{}", from_file.display(), import.path)
}

fn is_relative_import(path: &str) -> bool {
    path == "." || path == ".." || path.starts_with("./") || path.starts_with("../")
}

fn split_relative_segments(path: &str) -> (usize, Vec<&str>) {
    let mut parent_levels = 0_usize;
    let mut rest = path;

    while let Some(tail) = rest.strip_prefix("../") {
        parent_levels += 1;
        rest = tail;
    }
    if let Some(tail) = rest.strip_prefix("./") {
        rest = tail;
    }

    let segments = rest
        .split('.')
        .filter(|segment| !segment.is_empty())
        .collect::<Vec<_>>();
    (parent_levels, segments)
}

fn find_module_candidates(root: &Path, segments: &[&str]) -> Option<PathBuf> {
    if segments.is_empty() {
        let main = root.join("main.tg");
        return canonical_if_file(&main);
    }

    let candidate = segments
        .iter()
        .fold(root.to_path_buf(), |path, segment| path.join(segment));
    let file = candidate.with_extension("tg");
    if let Some(path) = canonical_if_file(&file) {
        return Some(path);
    }

    let nested_main = candidate.join("main.tg");
    canonical_if_file(&nested_main)
}

fn canonical_if_file(path: &Path) -> Option<PathBuf> {
    if !path.is_file() {
        return None;
    }
    path.canonicalize().ok()
}

fn collect_module_names(root: &Path, prefix: String, out: &mut BTreeSet<String>) {
    let Ok(entries) = fs::read_dir(root) else {
        return;
    };

    for entry in entries.flatten() {
        let path = entry.path();
        let Some(name) = path.file_stem().and_then(|stem| stem.to_str()) else {
            continue;
        };

        if path.is_file() && path.extension().and_then(|ext| ext.to_str()) == Some("tg") {
            if name == "main" {
                if !prefix.is_empty() {
                    out.insert(prefix.clone());
                }
            } else if prefix.is_empty() {
                out.insert(name.to_string());
            } else {
                out.insert(format!("{prefix}.{name}"));
            }
            continue;
        }

        if path.is_dir() {
            let next_prefix = if prefix.is_empty() {
                entry.file_name().to_string_lossy().into_owned()
            } else {
                format!("{prefix}.{}", entry.file_name().to_string_lossy())
            };
            collect_module_names(&path, next_prefix, out);
        }
    }
}

fn similarity_score(left: &str, right: &str) -> usize {
    longest_common_subsequence(left.as_bytes(), right.as_bytes())
}

fn longest_common_subsequence(left: &[u8], right: &[u8]) -> usize {
    let mut dp = vec![0_usize; right.len() + 1];
    for &left_byte in left {
        let mut prev = 0_usize;
        for (index, right_byte) in right.iter().enumerate() {
            let saved = dp[index + 1];
            dp[index + 1] = if left_byte == *right_byte {
                prev + 1
            } else {
                dp[index + 1].max(dp[index])
            };
            prev = saved;
        }
    }
    dp[right.len()]
}

fn load_dependency_versions(project_root: &Path) -> HashMap<String, String> {
    let mut versions = HashMap::new();
    let lockfile = project_root.join("drago.lock");
    let Ok(source) = fs::read_to_string(lockfile) else {
        return versions;
    };

    let mut in_dependencies = false;
    for line in source.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('[') && trimmed.ends_with(']') {
            in_dependencies = trimmed == "[dependencies]";
            continue;
        }
        if !in_dependencies || trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }

        let Some((name, raw_value)) = trimmed.split_once('=') else {
            continue;
        };
        let name = name.trim().to_string();
        let value = raw_value.trim().trim_matches('"').to_string();
        if !name.is_empty() && !value.is_empty() {
            versions.insert(name, value);
        }
    }

    versions
}
