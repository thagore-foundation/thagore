//! Tree-walking interpreter used by the browser playground.

mod env;
mod eval;
mod stdlib;
mod value;

use std::collections::HashMap;

use thagore_ast::{Decl, FuncDecl, InternedStr};

pub use crate::env::EnvStack;
pub use crate::stdlib::StdlibRegistry;
pub use crate::value::Value;

/// Runtime failures that can occur while interpreting Thagore code.
#[derive(Debug, Clone, PartialEq)]
pub enum RuntimeError {
    /// A recursive call chain exceeded the interpreter guard.
    StackOverflow,
    /// The step-count guard tripped.
    Timeout {
        /// Number of interpreter steps executed before aborting.
        steps: usize,
    },
    /// Captured program output exceeded the configured buffer size.
    OutputTooLarge,
    /// Attempted division by zero.
    DivisionByZero,
    /// Unsupported language feature in the browser interpreter.
    Unsupported(String),
    /// Generic runtime failure with a user-facing message.
    Message(String),
}

impl RuntimeError {
    /// Creates a generic runtime error from displayable text.
    #[must_use]
    pub fn message(message: impl Into<String>) -> Self {
        Self::Message(message.into())
    }

    /// Creates an unsupported-feature runtime error.
    #[must_use]
    pub fn unsupported(message: impl Into<String>) -> Self {
        Self::Unsupported(message.into())
    }
}

/// Symbol table that resolves parser-local `InternedStr` handles back to text.
#[derive(Debug, Clone, Default)]
pub struct SymbolTable {
    names: Vec<String>,
}

impl SymbolTable {
    /// Builds a symbol table snapshot from parser-interned names.
    #[must_use]
    pub fn from_snapshot(snapshot: &[&str]) -> Self {
        Self {
            names: snapshot.iter().map(|name| (*name).to_string()).collect(),
        }
    }

    /// Resolves `symbol` to its textual spelling.
    #[must_use]
    pub fn resolve(&self, symbol: InternedStr) -> Option<&str> {
        self.names.get(symbol.as_u32() as usize).map(String::as_str)
    }

    /// Returns the textual snapshot in parser-intern order.
    #[must_use]
    pub fn names(&self) -> &[String] {
        &self.names
    }
}

/// Tree-walking interpreter for a single parsed Thagore program.
#[derive(Debug, Clone)]
pub struct Interpreter<'ast> {
    env: EnvStack,
    functions: HashMap<String, &'ast FuncDecl<'ast>>,
    struct_names: Vec<String>,
    symbols: SymbolTable,
    stdlib: StdlibRegistry,
    stdout: String,
    stderr: String,
    stdin: String,
    stdin_cursor: usize,
    call_depth: usize,
    max_call_depth: usize,
    step_count: usize,
    max_steps: usize,
    max_output_bytes: usize,
}

impl<'ast> Interpreter<'ast> {
    /// Creates a new interpreter with default safety limits.
    #[must_use]
    pub fn new(symbols: SymbolTable) -> Self {
        Self {
            env: EnvStack::new(),
            functions: HashMap::new(),
            struct_names: Vec::new(),
            symbols,
            stdlib: StdlibRegistry::new(),
            stdout: String::new(),
            stderr: String::new(),
            stdin: String::new(),
            stdin_cursor: 0,
            call_depth: 0,
            max_call_depth: 1_000,
            step_count: 0,
            max_steps: 10_000_000,
            max_output_bytes: 1_048_576,
        }
    }

    /// Replaces the interpreter stdin buffer.
    pub fn set_stdin(&mut self, stdin: impl Into<String>) {
        self.stdin = stdin.into();
        self.stdin_cursor = 0;
    }

    /// Runs a parsed Thagore compilation unit and returns `main()`'s result.
    pub fn run(&mut self, decls: &'ast [Decl<'ast>]) -> Result<Value, RuntimeError> {
        self.install_default_bindings();
        self.collect_declarations(decls)?;
        if self.functions.contains_key("main") {
            self.call_func("main", Vec::new())
        } else {
            Ok(Value::Void)
        }
    }

    /// Returns the captured stdout buffer.
    #[must_use]
    pub fn stdout(&self) -> &str {
        &self.stdout
    }

    /// Returns the captured stderr buffer.
    #[must_use]
    pub fn stderr(&self) -> &str {
        &self.stderr
    }

    pub(crate) fn name(&self, symbol: InternedStr) -> Result<String, RuntimeError> {
        self.symbols
            .resolve(symbol)
            .map(ToString::to_string)
            .ok_or_else(|| RuntimeError::message(format!("unknown symbol id {}", symbol.as_u32())))
    }

    pub(crate) fn tick(&mut self) -> Result<(), RuntimeError> {
        self.step_count = self.step_count.saturating_add(1);
        if self.step_count > self.max_steps {
            return Err(RuntimeError::Timeout {
                steps: self.step_count,
            });
        }
        Ok(())
    }

    pub(crate) fn write_stdout(&mut self, text: &str) -> Result<(), RuntimeError> {
        self.check_output_growth(text.len())?;
        self.stdout.push_str(text);
        Ok(())
    }

    pub(crate) fn write_stdout_value(
        &mut self,
        value: &Value,
        trailing_newline: bool,
    ) -> Result<(), RuntimeError> {
        let mut rendered = String::new();
        value.render_into(&mut rendered);
        if trailing_newline {
            rendered.push('\n');
        }
        self.write_stdout(&rendered)
    }

    pub(crate) fn write_stderr(&mut self, text: &str) -> Result<(), RuntimeError> {
        self.check_output_growth(text.len())?;
        self.stderr.push_str(text);
        Ok(())
    }

    pub(crate) fn write_stderr_value(
        &mut self,
        value: &Value,
        trailing_newline: bool,
    ) -> Result<(), RuntimeError> {
        let mut rendered = String::new();
        value.render_into(&mut rendered);
        if trailing_newline {
            rendered.push('\n');
        }
        self.write_stderr(&rendered)
    }

    fn check_output_growth(&self, additional: usize) -> Result<(), RuntimeError> {
        let current = self.stdout.len().saturating_add(self.stderr.len());
        if current.saturating_add(additional) > self.max_output_bytes {
            Err(RuntimeError::OutputTooLarge)
        } else {
            Ok(())
        }
    }

    fn install_default_bindings(&mut self) {
        for builtin in ["print", "println", "eprint", "eprintln", "flush"] {
            self.env
                .define(builtin.to_string(), Value::Callable(builtin.to_string()));
        }
    }

    pub(crate) fn read_line(&mut self) -> String {
        if self.stdin_cursor >= self.stdin.len() {
            return String::new();
        }
        let rest = &self.stdin[self.stdin_cursor..];
        if let Some(index) = rest.find('\n') {
            let line = rest[..index].to_string();
            self.stdin_cursor += index + 1;
            line
        } else {
            self.stdin_cursor = self.stdin.len();
            rest.to_string()
        }
    }

    pub(crate) fn read_word(&mut self) -> Result<String, RuntimeError> {
        while let Some(ch) = self.stdin[self.stdin_cursor..].chars().next() {
            if ch.is_whitespace() {
                self.stdin_cursor += ch.len_utf8();
            } else {
                break;
            }
            if self.stdin_cursor >= self.stdin.len() {
                return Err(RuntimeError::message("stdin exhausted"));
            }
        }
        if self.stdin_cursor >= self.stdin.len() {
            return Err(RuntimeError::message("stdin exhausted"));
        }
        let start = self.stdin_cursor;
        while let Some(ch) = self.stdin[self.stdin_cursor..].chars().next() {
            if ch.is_whitespace() {
                break;
            }
            self.stdin_cursor += ch.len_utf8();
            if self.stdin_cursor >= self.stdin.len() {
                break;
            }
        }
        Ok(self.stdin[start..self.stdin_cursor].to_string())
    }

    pub(crate) fn read_all(&mut self) -> String {
        let rest = self.stdin[self.stdin_cursor..].to_string();
        self.stdin_cursor = self.stdin.len();
        rest
    }
}
