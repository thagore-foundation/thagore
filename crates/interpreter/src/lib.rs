//! Tree-walking interpreter used by the browser playground.

mod env;
mod eval;
mod stdlib;
mod value;

use std::collections::{HashMap, HashSet};

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
    /// ID-keyed parallel index into `functions` for zero-allocation callee resolution.
    functions_by_id: HashMap<u32, &'ast FuncDecl<'ast>>,
    const_symbols: HashSet<String>,
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
            functions_by_id: HashMap::new(),
            const_symbols: HashSet::new(),
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

    /// Creates an interpreter with a custom step limit (for benchmarks and testing).
    #[must_use]
    pub fn with_max_steps(symbols: SymbolTable, max_steps: usize) -> Self {
        Self {
            max_steps,
            ..Self::new(symbols)
        }
    }

    /// Replaces the interpreter stdin buffer.
    pub fn set_stdin(&mut self, stdin: impl Into<String>) {
        self.stdin = stdin.into();
        self.stdin_cursor = 0;
    }

    /// Runs a parsed Thagore compilation unit and returns `main()`'s result.
    pub fn run(&mut self, decls: &'ast [Decl<'ast>]) -> Result<Value, RuntimeError> {
        self.reset_program_state();
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

    /// Resolves a symbol to a `&str` slice without allocating.
    pub(crate) fn name_ref(&self, symbol: InternedStr) -> Result<&str, RuntimeError> {
        self.symbols
            .resolve(symbol)
            .ok_or_else(|| RuntimeError::message(format!("unknown symbol id {}", symbol.as_u32())))
    }

    pub(crate) fn name(&self, symbol: InternedStr) -> Result<String, RuntimeError> {
        self.name_ref(symbol).map(str::to_string)
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

    fn reset_program_state(&mut self) {
        self.env = EnvStack::new();
        self.functions.clear();
        self.functions_by_id.clear();
        self.const_symbols.clear();
        self.struct_names.clear();
        self.stdout.clear();
        self.stderr.clear();
        self.stdin_cursor = 0;
        self.call_depth = 0;
        self.step_count = 0;
    }
}

#[cfg(test)]
mod tests {
    use super::{Interpreter, RuntimeError, SymbolTable, Value};
    use thagore_ast::{
        Block, ConstDecl, Decl, ExternDecl, FlowDecl, FlowStage, FuncDecl, ImplBlock, InternedStr,
        IntentDecl, LetDecl, LitExpr, Literal, NamedTypeExpr, NodeId, Span, StructDecl, TypeExpr,
    };

    fn span() -> Span {
        Span::empty()
    }

    fn leak_value<T>(value: T) -> &'static T {
        Box::leak(Box::new(value))
    }

    fn leak_slice<T>(items: Vec<T>) -> &'static [T] {
        Box::leak(items.into_boxed_slice())
    }

    fn symbol_table(names: &[&str]) -> SymbolTable {
        SymbolTable::from_snapshot(names)
    }

    fn empty_block() -> &'static Block<'static> {
        leak_value(Block {
            id: NodeId::new(0),
            span: span(),
            statements: &[],
        })
    }

    fn int_literal(value: i64) -> &'static thagore_ast::Expr<'static> {
        leak_value(thagore_ast::Expr::Literal(LitExpr {
            id: NodeId::new(1),
            span: span(),
            literal: Literal::Int(value),
        }))
    }

    fn ident(symbol: InternedStr) -> &'static thagore_ast::Expr<'static> {
        leak_value(thagore_ast::Expr::Ident(thagore_ast::IdentExpr {
            id: NodeId::new(12),
            span: span(),
            name: symbol,
        }))
    }

    fn call(
        callee: &'static thagore_ast::Expr<'static>,
        args: Vec<&'static thagore_ast::Expr<'static>>,
    ) -> &'static thagore_ast::Expr<'static> {
        leak_value(thagore_ast::Expr::Call(thagore_ast::CallExpr {
            id: NodeId::new(13),
            span: span(),
            callee,
            args: leak_slice(args),
        }))
    }

    fn named_type(symbol: InternedStr) -> &'static TypeExpr<'static> {
        leak_value(TypeExpr::Named(NamedTypeExpr {
            id: NodeId::new(2),
            span: span(),
            name: symbol,
        }))
    }

    #[test]
    fn rejects_top_level_let_declarations() {
        let mut interpreter = Interpreter::new(symbol_table(&["answer"]));
        let decls = [Decl::Let(LetDecl {
            id: NodeId::new(3),
            span: span(),
            name: InternedStr::new(0),
            ty: None,
            initializer: int_literal(42),
        })];

        let result = interpreter.run(&decls);

        assert_eq!(
            result,
            Err(RuntimeError::unsupported(
                "top-level let declarations are not supported in the playground interpreter",
            ))
        );
    }

    #[test]
    fn allows_extern_decls_as_noops() {
        // The WASM playground prepends synthetic extern decls for host builtins
        // (println, from_int, etc.) before evaluating user code. The interpreter
        // must accept them silently — actual dispatch happens via
        // install_default_bindings.
        let extern_decls = [Decl::Extern(ExternDecl {
            id: NodeId::new(4),
            span: span(),
            name: InternedStr::new(0),
            params: &[],
            return_type: named_type(InternedStr::new(1)),
        })];
        let mut interpreter = Interpreter::new(symbol_table(&["ffi_call", "i32"]));
        assert!(interpreter.run(&extern_decls).is_ok());
    }

    #[test]
    fn rejects_unsupported_top_level_declarations() {
        let impl_decls = [Decl::Impl(ImplBlock {
            id: NodeId::new(5),
            span: span(),
            target: InternedStr::new(0),
            methods: &[],
        })];
        let mut interpreter = Interpreter::new(symbol_table(&["Widget"]));
        assert_eq!(
            interpreter.run(&impl_decls),
            Err(RuntimeError::unsupported(
                "impl blocks are not supported in the playground interpreter",
            ))
        );

        let intent_decls = [Decl::Intent(IntentDecl {
            id: NodeId::new(6),
            span: span(),
            name: InternedStr::new(0),
            constraints: &[],
            body: empty_block(),
        })];
        let mut interpreter = Interpreter::new(symbol_table(&["ship_order"]));
        assert_eq!(
            interpreter.run(&intent_decls),
            Err(RuntimeError::unsupported(
                "intent declarations are not supported in the playground interpreter",
            ))
        );

        let flow_decls = [Decl::Flow(FlowDecl {
            id: NodeId::new(7),
            span: span(),
            name: InternedStr::new(0),
            stages: leak_slice(vec![FlowStage {
                id: NodeId::new(8),
                span: span(),
                name: InternedStr::new(1),
                body: empty_block(),
            }]),
            compensation: None,
        })];
        let mut interpreter = Interpreter::new(symbol_table(&["checkout", "charge"]));
        assert_eq!(
            interpreter.run(&flow_decls),
            Err(RuntimeError::unsupported(
                "flow declarations are not supported in the playground interpreter",
            ))
        );
    }

    #[test]
    fn still_runs_supported_top_level_declarations() {
        let decls = [
            Decl::Const(ConstDecl {
                id: NodeId::new(9),
                span: span(),
                name: InternedStr::new(0),
                type_ann: named_type(InternedStr::new(1)),
                value: int_literal(7),
            }),
            Decl::Struct(StructDecl {
                id: NodeId::new(10),
                span: span(),
                name: InternedStr::new(2),
                fields: &[],
            }),
            Decl::Func(FuncDecl {
                id: NodeId::new(11),
                span: span(),
                name: InternedStr::new(3),
                params: &[],
                return_type: None,
                body: empty_block(),
            }),
        ];
        let mut interpreter = Interpreter::new(symbol_table(&["answer", "i32", "Widget", "main"]));

        let result = interpreter.run(&decls);

        assert_eq!(result, Ok(Value::Void));
        assert_eq!(interpreter.env.get_by_id(0), Some(Value::I32(7)));
    }

    #[test]
    fn resets_program_state_between_runs() {
        let first = [
            Decl::Const(ConstDecl {
                id: NodeId::new(16),
                span: span(),
                name: InternedStr::new(0),
                type_ann: named_type(InternedStr::new(1)),
                value: int_literal(7),
            }),
            Decl::Func(FuncDecl {
                id: NodeId::new(17),
                span: span(),
                name: InternedStr::new(2),
                params: &[],
                return_type: None,
                body: empty_block(),
            }),
        ];
        let second = [Decl::Func(FuncDecl {
            id: NodeId::new(18),
            span: span(),
            name: InternedStr::new(2),
            params: &[],
            return_type: None,
            body: empty_block(),
        })];
        let mut interpreter = Interpreter::new(symbol_table(&["answer", "i32", "main"]));

        assert_eq!(interpreter.run(&first), Ok(Value::Void));
        assert_eq!(interpreter.env.get_by_id(0), Some(Value::I32(7)));

        assert_eq!(interpreter.run(&second), Ok(Value::Void));
        assert_eq!(interpreter.env.get_by_id(0), None);
    }

    #[test]
    fn rejects_non_constant_top_level_const_initializers() {
        let decls = [
            Decl::Func(FuncDecl {
                id: NodeId::new(14),
                span: span(),
                name: InternedStr::new(0),
                params: &[],
                return_type: None,
                body: empty_block(),
            }),
            Decl::Const(ConstDecl {
                id: NodeId::new(15),
                span: span(),
                name: InternedStr::new(1),
                type_ann: named_type(InternedStr::new(2)),
                value: call(ident(InternedStr::new(0)), Vec::new()),
            }),
        ];
        let mut interpreter = Interpreter::new(symbol_table(&["main", "answer", "i32"]));

        let result = interpreter.run(&decls);

        assert_eq!(
            result,
            Err(RuntimeError::message(
                "top-level const initializers must be compile-time constant expressions",
            ))
        );
    }
}
