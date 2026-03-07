use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use thagore_module_graph::{ModuleGraph, ModuleResolver, ModuleSource};

fn temp_root(name: &str) -> PathBuf {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("clock")
        .as_nanos();
    let root = std::env::temp_dir().join(format!("thagore_module_graph_{name}_{nonce}"));
    fs::create_dir_all(&root).expect("create temp root");
    root
}

fn write_file(path: &Path, source: &str) {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).expect("create parent");
    }
    fs::write(path, source).expect("write file");
}

#[test]
fn resolver_prefers_project_std_before_bundled_stdlib() {
    let root = temp_root("resolver_project_std");
    let project_root = root.join("project");
    let stdlib_root = root.join("stdlib");
    write_file(&project_root.join("src/main.tg"), "import std.string\n");
    write_file(&project_root.join("std/string.tg"), "func project_string() -> i32:\n  return 0\n");
    write_file(&stdlib_root.join("string.tg"), "func bundled_string() -> i32:\n  return 0\n");

    let mut resolver = ModuleResolver::new(stdlib_root.clone(), project_root.clone(), Vec::new());
    let graph = ModuleGraph::build(&project_root.join("src/main.tg"), &mut resolver).expect("build");

    let imported = graph
        .nodes
        .values()
        .find(|module| module.file_path != project_root.join("src/main.tg").canonicalize().unwrap())
        .expect("imported module");
    assert_eq!(imported.source, ModuleSource::Project);
    assert!(imported.file_path.ends_with("project/std/string.tg"));
}

#[test]
fn resolver_handles_relative_imports_without_self_cycles() {
    let root = temp_root("resolver_relative");
    let project_root = root.join("project");
    let stdlib_root = root.join("stdlib");
    write_file(
        &project_root.join("src/main.tg"),
        "from . import helper\nfunc main() -> i32:\n  return helper.value()\n",
    );
    write_file(
        &project_root.join("src/helper.tg"),
        "func value() -> i32:\n  return 1\n",
    );

    let mut resolver = ModuleResolver::new(stdlib_root, project_root.clone(), Vec::new());
    let graph = ModuleGraph::build(&project_root.join("src/main.tg"), &mut resolver).expect("build");
    assert_eq!(graph.nodes.len(), 2);
    assert_eq!(graph.topo_order.len(), 2);
}

#[test]
fn graph_topo_order_places_dependency_before_entry() {
    let root = temp_root("resolver_topo");
    let project_root = root.join("project");
    let stdlib_root = root.join("stdlib");
    write_file(
        &project_root.join("src/main.tg"),
        "import src.util\nfunc main() -> i32:\n  return src.util.answer()\n",
    );
    write_file(&project_root.join("src/util.tg"), "func answer() -> i32:\n  return 42\n");

    let mut resolver = ModuleResolver::new(stdlib_root, project_root.clone(), Vec::new());
    let graph = ModuleGraph::build(&project_root.join("src/main.tg"), &mut resolver).expect("build");
    let entry = graph
        .nodes
        .iter()
        .find_map(|(id, module)| module.file_path.ends_with("src/main.tg").then_some(*id))
        .expect("entry");
    let util = graph
        .nodes
        .iter()
        .find_map(|(id, module)| module.file_path.ends_with("src/util.tg").then_some(*id))
        .expect("util");
    let util_pos = graph.topo_order.iter().position(|id| *id == util).expect("util pos");
    let entry_pos = graph.topo_order.iter().position(|id| *id == entry).expect("entry pos");
    assert!(util_pos < entry_pos);
}
