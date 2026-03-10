//! Runtime value model for the Thagore interpreter.

use std::collections::HashMap;
use std::fmt;

/// A runtime value produced by evaluating Thagore code.
#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    /// Signed 32-bit integer.
    I32(i32),
    /// Signed 64-bit integer.
    I64(i64),
    /// Double-precision floating-point number.
    F64(f64),
    /// Boolean value.
    Bool(bool),
    /// UTF-8 string value.
    Str(String),
    /// Unit / void value.
    Void,
    /// Heterogeneous vector value used by interpreter-side stdlib helpers.
    Vec(Vec<Value>),
    /// Struct instance value.
    Struct {
        /// Struct type name.
        name: String,
        /// Field values by field name.
        fields: HashMap<String, Value>,
    },
    /// Early return propagation sentinel.
    Return(Box<Value>),
    /// Loop break propagation sentinel.
    Break,
    /// Loop continue propagation sentinel.
    Continue,
    /// Imported module namespace placeholder.
    Module(String),
    /// Callable handle for builtin or user functions.
    Callable(String),
}

impl Value {
    /// Returns the human-readable runtime type name of this value.
    #[must_use]
    pub fn type_name(&self) -> &'static str {
        match self {
            Self::I32(_) => "i32",
            Self::I64(_) => "i64",
            Self::F64(_) => "f64",
            Self::Bool(_) => "bool",
            Self::Str(_) => "str",
            Self::Void => "void",
            Self::Vec(_) => "vec",
            Self::Struct { .. } => "struct",
            Self::Return(_) => "return",
            Self::Break => "break",
            Self::Continue => "continue",
            Self::Module(_) => "module",
            Self::Callable(_) => "callable",
        }
    }

    /// Returns the value rendered using Thagore-facing text conventions.
    #[must_use]
    pub fn render(&self) -> String {
        let mut rendered = String::new();
        self.render_into(&mut rendered);
        rendered
    }

    /// Appends the rendered representation to an existing string buffer.
    pub fn render_into(&self, output: &mut String) {
        match self {
            Self::I32(value) => output.push_str(&value.to_string()),
            Self::I64(value) => output.push_str(&value.to_string()),
            Self::F64(value) => {
                let text = value.to_string();
                if text.contains('.') {
                    output.push_str(&text);
                } else {
                    output.push_str(&text);
                    output.push_str(".0");
                }
            }
            Self::Bool(value) => output.push_str(if *value { "true" } else { "false" }),
            Self::Str(value) => output.push_str(value),
            Self::Void => {}
            Self::Vec(values) => {
                output.push('[');
                for (index, value) in values.iter().enumerate() {
                    if index > 0 {
                        output.push_str(", ");
                    }
                    value.render_into(output);
                }
                output.push(']');
            }
            Self::Struct { name, fields } => {
                output.push_str(name);
                output.push_str(" { ");
                let mut entries = fields.iter().collect::<Vec<_>>();
                entries.sort_by(|left, right| left.0.cmp(right.0));
                for (index, (field, value)) in entries.into_iter().enumerate() {
                    if index > 0 {
                        output.push_str(", ");
                    }
                    output.push_str(field);
                    output.push_str(": ");
                    value.render_into(output);
                }
                output.push_str(" }");
            }
            Self::Return(value) => value.render_into(output),
            Self::Break => output.push_str("<break>"),
            Self::Continue => output.push_str("<continue>"),
            Self::Module(name) => {
                output.push_str("<module ");
                output.push_str(name);
                output.push('>');
            }
            Self::Callable(name) => {
                output.push_str("<callable ");
                output.push_str(name);
                output.push('>');
            }
        }
    }

    /// Returns `true` when this value is a control-flow sentinel.
    #[must_use]
    pub fn is_control_flow(&self) -> bool {
        matches!(self, Self::Return(_) | Self::Break | Self::Continue)
    }
}

impl fmt::Display for Value {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.render())
    }
}
