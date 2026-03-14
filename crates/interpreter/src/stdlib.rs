//! Interpreter-side standard library implemented in pure Rust.

use std::collections::HashMap;
use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use crate::value::Value;
use crate::{Interpreter, RuntimeError};

type BuiltinHandler = fn(&mut Interpreter<'_>, Vec<Value>) -> Result<Value, RuntimeError>;

/// Registry of builtin callables and module exports available to the interpreter.
#[derive(Debug, Clone)]
pub struct StdlibRegistry {
    handlers: HashMap<String, BuiltinHandler>,
    modules: HashMap<String, Vec<String>>,
}

impl Default for StdlibRegistry {
    fn default() -> Self {
        Self::new()
    }
}

impl StdlibRegistry {
    /// Creates the default interpreter stdlib registry.
    #[must_use]
    pub fn new() -> Self {
        let mut registry = Self {
            handlers: HashMap::new(),
            modules: HashMap::new(),
        };

        registry.register("print", builtin_print);
        registry.register("println", builtin_println);
        registry.register("eprint", builtin_eprint);
        registry.register("eprintln", builtin_eprintln);
        registry.register("flush", builtin_flush);
        registry.register("from_int", builtin_from_int);
        registry.register("from_f64", builtin_from_f64);
        registry.register("from_bool", builtin_from_bool);
        registry.register("to_int", builtin_to_int);
        registry.register("to_f64", builtin_to_f64);
        registry.register("to_bool", builtin_to_bool);
        registry.register("len", builtin_len);
        registry.register("concat", builtin_concat);
        registry.register("trim", builtin_trim);
        registry.register("contains", builtin_contains);
        registry.register("starts_with", builtin_starts_with);
        registry.register("ends_with", builtin_ends_with);
        registry.register("find", builtin_find);
        registry.register("replace", builtin_replace);
        registry.register("to_upper", builtin_to_upper);
        registry.register("to_lower", builtin_to_lower);
        registry.register("pad_left", builtin_pad_left);
        registry.register("pad_right", builtin_pad_right);
        registry.register("repeat", builtin_repeat);
        registry.register("reverse", builtin_reverse);
        registry.register("strip", builtin_strip);
        registry.register("char_at", builtin_char_at);
        registry.register("split", builtin_split);
        registry.register("join", builtin_join);
        registry.register("is_empty", builtin_is_empty);
        registry.register("abs", builtin_abs);
        registry.register("min", builtin_min);
        registry.register("max", builtin_max);
        registry.register("clamp", builtin_clamp);
        registry.register("pow", builtin_pow);
        registry.register("sqrt", builtin_sqrt);
        registry.register("floor", builtin_floor);
        registry.register("ceil", builtin_ceil);
        registry.register("round", builtin_round);
        registry.register("log", builtin_log);
        registry.register("log2", builtin_log2);
        registry.register("log10", builtin_log10);
        registry.register("gcd", builtin_gcd);
        registry.register("lcm", builtin_lcm);
        registry.register("is_even", builtin_is_even);
        registry.register("is_odd", builtin_is_odd);
        registry.register("now_ms", builtin_now_ms);
        registry.register("sleep_ms", builtin_sleep_ms);
        registry.register("read_line", builtin_read_line);
        registry.register("read_int", builtin_read_int);
        registry.register("read_i64", builtin_read_i64);
        registry.register("read_f64", builtin_read_f64);
        registry.register("read_word", builtin_read_word);
        registry.register("read_all", builtin_read_all);
        registry.register("read_ints", builtin_read_ints);
        registry.register("read_i64s", builtin_read_i64s);
        registry.register("read", builtin_fs_read);
        registry.register("write", builtin_fs_write);
        registry.register("exists", builtin_fs_exists);
        registry.register("mkdir", builtin_fs_mkdir);
        registry.register("read_dir", builtin_fs_read_dir);
        registry.register("remove", builtin_fs_remove);
        registry.register("getcwd", builtin_fs_getcwd);
        registry.register("path_join", builtin_fs_path_join);
        registry.register("is_dir", builtin_fs_is_dir);
        registry.register("filesize", builtin_fs_filesize);
        registry.register("run", builtin_process_run);
        registry.register("capture", builtin_process_capture);
        registry.register("argv", builtin_process_argv);
        registry.register("argc", builtin_process_argc);
        registry.register("env", builtin_process_env);
        registry.register("exit", builtin_process_exit);

        registry.register_module(
            "math",
            &[
                "abs", "min", "max", "clamp", "pow", "sqrt", "floor", "ceil", "round", "log",
                "log2", "log10", "gcd", "lcm", "is_even", "is_odd",
            ],
        );
        registry.register_module(
            "string",
            &[
                "len",
                "concat",
                "split",
                "trim",
                "contains",
                "starts_with",
                "ends_with",
                "find",
                "replace",
                "to_upper",
                "to_lower",
                "pad_left",
                "pad_right",
                "repeat",
                "reverse",
                "strip",
                "char_at",
                "to_int",
                "to_f64",
                "to_bool",
                "from_int",
                "from_f64",
                "from_bool",
                "is_empty",
                "join",
            ],
        );
        registry.register_module(
            "io",
            &[
                "print",
                "println",
                "eprint",
                "eprintln",
                "flush",
                "read_line",
                "read_int",
                "read_i64",
                "read_f64",
                "read_word",
                "read_all",
                "read_ints",
                "read_i64s",
            ],
        );
        registry.register_module("time", &["now_ms", "sleep_ms"]);
        registry.register_module(
            "fs",
            &[
                "read",
                "write",
                "exists",
                "mkdir",
                "read_dir",
                "remove",
                "getcwd",
                "path_join",
                "is_dir",
                "filesize",
            ],
        );
        registry.register_module(
            "process",
            &[
                "run",
                "capture",
                "argv",
                "argc",
                "env",
                "exit",
            ],
        );

        registry
    }

    /// Returns `true` when `name` resolves to a registered builtin callable.
    #[must_use]
    pub fn has_callable(&self, name: &str) -> bool {
        self.handlers.contains_key(name)
    }

    /// Returns the builtin callable handler for `name`, if present.
    #[must_use]
    pub fn handler(&self, name: &str) -> Option<BuiltinHandler> {
        self.handlers.get(name).copied()
    }

    /// Returns exported symbol names for a stdlib module.
    #[must_use]
    pub fn module_exports(&self, module: &str) -> Option<&[String]> {
        self.modules.get(module).map(Vec::as_slice)
    }

    /// Resolves a module-qualified builtin symbol like `math.sqrt`.
    #[must_use]
    pub fn resolve_module_symbol(&self, module: &str, symbol: &str) -> Option<String> {
        let exports = self.module_exports(module)?;
        exports
            .iter()
            .find(|candidate| candidate.as_str() == symbol)
            .map(|candidate| candidate.clone())
    }

    fn register(&mut self, name: &str, handler: BuiltinHandler) {
        self.handlers.insert(name.to_string(), handler);
    }

    fn register_module(&mut self, module: &str, exports: &[&str]) {
        self.modules.insert(
            module.to_string(),
            exports.iter().map(|name| (*name).to_string()).collect(),
        );
    }
}

fn expect_arity(args: &[Value], expected: usize, name: &str) -> Result<(), RuntimeError> {
    if args.len() == expected {
        Ok(())
    } else {
        Err(RuntimeError::message(format!(
            "{name} expected {expected} arguments, found {}",
            args.len()
        )))
    }
}

fn expect_string(value: &Value, name: &str) -> Result<String, RuntimeError> {
    match value {
        Value::Str(text) => Ok(text.clone()),
        other => Err(RuntimeError::message(format!(
            "{name} expected str, found {}",
            other.type_name()
        ))),
    }
}

fn expect_i32(value: &Value, name: &str) -> Result<i32, RuntimeError> {
    match value {
        Value::I32(number) => Ok(*number),
        other => Err(RuntimeError::message(format!(
            "{name} expected i32, found {}",
            other.type_name()
        ))),
    }
}

fn expect_i64(value: &Value, name: &str) -> Result<i64, RuntimeError> {
    match value {
        Value::I64(number) => Ok(*number),
        other => Err(RuntimeError::message(format!(
            "{name} expected i64, found {}",
            other.type_name()
        ))),
    }
}

fn expect_f64(value: &Value, name: &str) -> Result<f64, RuntimeError> {
    match value {
        Value::F64(number) => Ok(*number),
        other => Err(RuntimeError::message(format!(
            "{name} expected f64, found {}",
            other.type_name()
        ))),
    }
}

fn parse_i32_like_runtime(text: &str) -> i32 {
    let trimmed = text.trim_start();
    let bytes = trimmed.as_bytes();
    let mut end = 0usize;

    if matches!(bytes.first(), Some(b'+') | Some(b'-')) {
        end += 1;
    }

    let digits_start = end;
    while end < bytes.len() && bytes[end].is_ascii_digit() {
        end += 1;
    }

    if end == digits_start {
        return 0;
    }

    trimmed[..end]
        .parse::<i64>()
        .map(|value| value.clamp(i32::MIN as i64, i32::MAX as i64) as i32)
        .unwrap_or(0)
}

fn parse_f64_like_runtime(text: &str) -> f64 {
    let trimmed = text.trim_start();
    let bytes = trimmed.as_bytes();
    let mut end = 0usize;

    if matches!(bytes.first(), Some(b'+') | Some(b'-')) {
        end += 1;
    }

    let int_start = end;
    while end < bytes.len() && bytes[end].is_ascii_digit() {
        end += 1;
    }
    let has_int = end > int_start;

    if end < bytes.len() && bytes[end] == b'.' {
        end += 1;
        let frac_start = end;
        while end < bytes.len() && bytes[end].is_ascii_digit() {
            end += 1;
        }
        if !has_int && end == frac_start {
            return 0.0;
        }
    } else if !has_int {
        return 0.0;
    }

    let exponent_start = end;
    if end < bytes.len() && matches!(bytes[end], b'e' | b'E') {
        let mut exp_end = end + 1;
        if exp_end < bytes.len() && matches!(bytes[exp_end], b'+' | b'-') {
            exp_end += 1;
        }
        let exp_digits_start = exp_end;
        while exp_end < bytes.len() && bytes[exp_end].is_ascii_digit() {
            exp_end += 1;
        }
        if exp_end > exp_digits_start {
            end = exp_end;
        } else {
            end = exponent_start;
        }
    }

    trimmed[..end].parse::<f64>().unwrap_or(0.0)
}

fn expect_numeric_pair(
    left: &Value,
    right: &Value,
    name: &str,
) -> Result<(Value, Value), RuntimeError> {
    match (left, right) {
        (Value::I32(_), Value::I32(_))
        | (Value::I64(_), Value::I64(_))
        | (Value::F64(_), Value::F64(_))
        | (Value::Str(_), Value::Str(_)) => Ok((left.clone(), right.clone())),
        _ => Err(RuntimeError::message(format!(
            "{name} expected matching comparable values, found {} and {}",
            left.type_name(),
            right.type_name()
        ))),
    }
}

fn builtin_print(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "print")?;
    interpreter.write_stdout_value(&args[0], false)?;
    Ok(Value::Void)
}

fn builtin_println(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "println")?;
    interpreter.write_stdout_value(&args[0], true)?;
    Ok(Value::Void)
}

fn builtin_eprint(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "eprint")?;
    interpreter.write_stderr_value(&args[0], false)?;
    Ok(Value::Void)
}

fn builtin_eprintln(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "eprintln")?;
    interpreter.write_stderr_value(&args[0], true)?;
    Ok(Value::Void)
}

fn builtin_flush(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 0, "flush")?;
    Ok(Value::Void)
}

fn builtin_from_int(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "from_int")?;
    let rendered = match args[0] {
        Value::I32(value) => value.to_string(),
        Value::I64(value) => value.to_string(),
        ref other => {
            return Err(RuntimeError::message(format!(
                "from_int expected i32 or i64, found {}",
                other.type_name()
            )))
        }
    };
    Ok(Value::Str(rendered))
}

fn builtin_from_f64(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "from_f64")?;
    Ok(Value::Str(expect_f64(&args[0], "from_f64")?.to_string()))
}

fn builtin_from_bool(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "from_bool")?;
    match args[0] {
        Value::Bool(value) => Ok(Value::Str(value.to_string())),
        ref other => Err(RuntimeError::message(format!(
            "from_bool expected bool, found {}",
            other.type_name()
        ))),
    }
}

fn builtin_to_int(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "to_int")?;
    let text = expect_string(&args[0], "to_int")?;
    Ok(Value::I32(parse_i32_like_runtime(&text)))
}

fn builtin_to_f64(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "to_f64")?;
    let text = expect_string(&args[0], "to_f64")?;
    Ok(Value::F64(parse_f64_like_runtime(&text)))
}

fn builtin_to_bool(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "to_bool")?;
    let text = expect_string(&args[0], "to_bool")?;
    Ok(Value::Bool(matches!(text.trim(), "true" | "1")))
}

fn builtin_len(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "len")?;
    match &args[0] {
        Value::Str(text) => Ok(Value::I32(text.len() as i32)),
        Value::Vec(values) => Ok(Value::I32(values.len() as i32)),
        other => Err(RuntimeError::message(format!(
            "len expected str or vec, found {}",
            other.type_name()
        ))),
    }
}

fn builtin_concat(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "concat")?;
    let left = expect_string(&args[0], "concat")?;
    let right = expect_string(&args[1], "concat")?;
    let mut result = String::with_capacity(left.len() + right.len());
    result.push_str(&left);
    result.push_str(&right);
    Ok(Value::Str(result))
}

fn builtin_trim(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "trim")?;
    Ok(Value::Str(
        expect_string(&args[0], "trim")?.trim().to_string(),
    ))
}

fn builtin_contains(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "contains")?;
    Ok(Value::Bool(
        expect_string(&args[0], "contains")?.contains(&expect_string(&args[1], "contains")?),
    ))
}

fn builtin_starts_with(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "starts_with")?;
    Ok(Value::Bool(
        expect_string(&args[0], "starts_with")?
            .starts_with(&expect_string(&args[1], "starts_with")?),
    ))
}

fn builtin_ends_with(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "ends_with")?;
    Ok(Value::Bool(
        expect_string(&args[0], "ends_with")?.ends_with(&expect_string(&args[1], "ends_with")?),
    ))
}

fn builtin_find(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "find")?;
    let haystack = expect_string(&args[0], "find")?;
    let needle = expect_string(&args[1], "find")?;
    Ok(Value::I32(
        haystack.find(&needle).map(|idx| idx as i32).unwrap_or(-1),
    ))
}

fn builtin_replace(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 3, "replace")?;
    let text = expect_string(&args[0], "replace")?;
    let old = expect_string(&args[1], "replace")?;
    let new = expect_string(&args[2], "replace")?;
    if old.is_empty() {
        return Ok(Value::Str(text));
    }
    Ok(Value::Str(text.replace(&old, &new)))
}

fn builtin_to_upper(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "to_upper")?;
    Ok(Value::Str(
        expect_string(&args[0], "to_upper")?.to_uppercase(),
    ))
}

fn builtin_to_lower(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "to_lower")?;
    Ok(Value::Str(
        expect_string(&args[0], "to_lower")?.to_lowercase(),
    ))
}

fn builtin_pad_left(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 3, "pad_left")?;
    let text = expect_string(&args[0], "pad_left")?;
    let width = expect_i32(&args[1], "pad_left")?.max(0) as usize;
    let ch = expect_string(&args[2], "pad_left")?
        .chars()
        .next()
        .unwrap_or(' ');
    if text.len() >= width {
        return Ok(Value::Str(text));
    }
    let padding = width - text.len();
    Ok(Value::Str(format!(
        "{}{}",
        ch.to_string().repeat(padding),
        text
    )))
}

fn builtin_pad_right(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 3, "pad_right")?;
    let text = expect_string(&args[0], "pad_right")?;
    let width = expect_i32(&args[1], "pad_right")?.max(0) as usize;
    let ch = expect_string(&args[2], "pad_right")?
        .chars()
        .next()
        .unwrap_or(' ');
    if text.len() >= width {
        return Ok(Value::Str(text));
    }
    let padding = width - text.len();
    Ok(Value::Str(format!(
        "{}{}",
        text,
        ch.to_string().repeat(padding)
    )))
}

fn builtin_repeat(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "repeat")?;
    Ok(Value::Str(
        expect_string(&args[0], "repeat")?.repeat(expect_i32(&args[1], "repeat")?.max(0) as usize),
    ))
}

fn builtin_reverse(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "reverse")?;
    Ok(Value::Str(
        expect_string(&args[0], "reverse")?.chars().rev().collect(),
    ))
}

fn builtin_strip(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "strip")?;
    let text = expect_string(&args[0], "strip")?;
    let chars = expect_string(&args[1], "strip")?;
    Ok(Value::Str(
        text.trim_matches(|ch| chars.contains(ch)).to_string(),
    ))
}

fn builtin_char_at(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "char_at")?;
    let text = expect_string(&args[0], "char_at")?;
    let index = expect_i32(&args[1], "char_at")?;
    if index < 0 {
        return Ok(Value::Str(String::new()));
    }
    Ok(Value::Str(
        text.chars()
            .nth(index as usize)
            .map(|ch| ch.to_string())
            .unwrap_or_default(),
    ))
}

fn builtin_split(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "split")?;
    let text = expect_string(&args[0], "split")?;
    let sep = expect_string(&args[1], "split")?;
    if sep.is_empty() {
        return Ok(Value::Vec(
            text.chars()
                .map(|part| Value::Str(part.to_string()))
                .collect(),
        ));
    }
    Ok(Value::Vec(
        text.split(&sep)
            .map(|part| Value::Str(part.to_string()))
            .collect(),
    ))
}

fn builtin_join(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "join")?;
    let parts = match &args[0] {
        Value::Vec(values) => {
            let mut rendered = Vec::with_capacity(values.len());
            for value in values {
                match value {
                    Value::Str(text) => rendered.push(text.clone()),
                    other => {
                        return Err(RuntimeError::message(format!(
                            "join expected vec[str], found {} element",
                            other.type_name()
                        )))
                    }
                }
            }
            rendered
        }
        other => {
            return Err(RuntimeError::message(format!(
                "join expected vec, found {}",
                other.type_name()
            )))
        }
    };
    let sep = expect_string(&args[1], "join")?;
    Ok(Value::Str(parts.join(&sep)))
}

fn builtin_is_empty(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "is_empty")?;
    match &args[0] {
        Value::Str(text) => Ok(Value::Bool(text.is_empty())),
        Value::Vec(values) => Ok(Value::Bool(values.is_empty())),
        other => Err(RuntimeError::message(format!(
            "is_empty expected str or vec, found {}",
            other.type_name()
        ))),
    }
}

fn builtin_abs(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "abs")?;
    match &args[0] {
        Value::I32(value) => Ok(Value::I32(value.abs())),
        Value::I64(value) => Ok(Value::I64(value.abs())),
        Value::F64(value) => Ok(Value::F64(value.abs())),
        other => Err(RuntimeError::message(format!(
            "abs expected numeric value, found {}",
            other.type_name()
        ))),
    }
}

fn builtin_min(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "min")?;
    let (left, right) = expect_numeric_pair(&args[0], &args[1], "min")?;
    Ok(match (left, right) {
        (Value::I32(a), Value::I32(b)) => Value::I32(a.min(b)),
        (Value::I64(a), Value::I64(b)) => Value::I64(a.min(b)),
        (Value::F64(a), Value::F64(b)) => Value::F64(a.min(b)),
        (Value::Str(a), Value::Str(b)) => Value::Str(if a <= b { a } else { b }),
        _ => unreachable!("validated pair"),
    })
}

fn builtin_max(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "max")?;
    let (left, right) = expect_numeric_pair(&args[0], &args[1], "max")?;
    Ok(match (left, right) {
        (Value::I32(a), Value::I32(b)) => Value::I32(a.max(b)),
        (Value::I64(a), Value::I64(b)) => Value::I64(a.max(b)),
        (Value::F64(a), Value::F64(b)) => Value::F64(a.max(b)),
        (Value::Str(a), Value::Str(b)) => Value::Str(if a >= b { a } else { b }),
        _ => unreachable!("validated pair"),
    })
}

fn builtin_clamp(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 3, "clamp")?;
    Ok(match (&args[0], &args[1], &args[2]) {
        (Value::I32(value), Value::I32(lo), Value::I32(hi)) => Value::I32((*value).clamp(*lo, *hi)),
        (Value::I64(value), Value::I64(lo), Value::I64(hi)) => Value::I64((*value).clamp(*lo, *hi)),
        (Value::F64(value), Value::F64(lo), Value::F64(hi)) => Value::F64((*value).clamp(*lo, *hi)),
        (Value::Str(value), Value::Str(lo), Value::Str(hi)) => {
            if value < lo {
                Value::Str(lo.clone())
            } else if value > hi {
                Value::Str(hi.clone())
            } else {
                Value::Str(value.clone())
            }
        }
        (left, middle, right) => {
            return Err(RuntimeError::message(format!(
                "clamp expected matching ordered values, found {}, {}, {}",
                left.type_name(),
                middle.type_name(),
                right.type_name()
            )))
        }
    })
}

fn builtin_pow(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "pow")?;
    Ok(Value::F64(
        expect_f64(&args[0], "pow")?.powf(expect_f64(&args[1], "pow")?),
    ))
}

fn builtin_sqrt(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "sqrt")?;
    Ok(Value::F64(expect_f64(&args[0], "sqrt")?.sqrt()))
}

fn builtin_floor(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "floor")?;
    Ok(Value::F64(expect_f64(&args[0], "floor")?.floor()))
}

fn builtin_ceil(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "ceil")?;
    Ok(Value::F64(expect_f64(&args[0], "ceil")?.ceil()))
}

fn builtin_round(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "round")?;
    Ok(Value::F64(expect_f64(&args[0], "round")?.round()))
}

fn builtin_log(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "log")?;
    Ok(Value::F64(expect_f64(&args[0], "log")?.ln()))
}

fn builtin_log2(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "log2")?;
    Ok(Value::F64(expect_f64(&args[0], "log2")?.log2()))
}

fn builtin_log10(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "log10")?;
    Ok(Value::F64(expect_f64(&args[0], "log10")?.log10()))
}

fn builtin_gcd(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "gcd")?;
    let mut a = expect_i32(&args[0], "gcd")?.abs();
    let mut b = expect_i32(&args[1], "gcd")?.abs();
    while b != 0 {
        let r = a % b;
        a = b;
        b = r;
    }
    Ok(Value::I32(a))
}

fn builtin_lcm(interpreter: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "lcm")?;
    let a = expect_i32(&args[0], "lcm")?;
    let b = expect_i32(&args[1], "lcm")?;
    let gcd = builtin_gcd(interpreter, vec![Value::I32(a), Value::I32(b)])?;
    let gcd = expect_i32(&gcd, "lcm")?;
    if gcd == 0 {
        return Ok(Value::I32(0));
    }
    Ok(Value::I32((a / gcd) * b))
}

fn builtin_is_even(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "is_even")?;
    Ok(Value::Bool(expect_i32(&args[0], "is_even")? % 2 == 0))
}

fn builtin_is_odd(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "is_odd")?;
    Ok(Value::Bool(expect_i32(&args[0], "is_odd")? % 2 != 0))
}

fn builtin_now_ms(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 0, "now_ms")?;
    let elapsed = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|error| RuntimeError::message(format!("now_ms failed: {error}")))?;
    let millis = elapsed.as_millis();
    let millis = i64::try_from(millis)
        .map_err(|_| RuntimeError::message("now_ms overflowed i64 range"))?;
    Ok(Value::I64(millis))
}

fn builtin_sleep_ms(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "sleep_ms")?;
    let millis = expect_i64(&args[0], "sleep_ms")?;
    if millis > 0 {
        thread::sleep(Duration::from_millis(millis as u64));
    }
    Ok(Value::Void)
}

fn builtin_read_line(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 0, "read_line")?;
    Ok(Value::Str(interpreter.read_line()))
}

fn builtin_read_int(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 0, "read_int")?;
    let word = interpreter.read_word()?;
    let parsed = word
        .parse::<i32>()
        .map_err(|_| RuntimeError::message(format!("read_int could not parse '{word}' as i32")))?;
    Ok(Value::I32(parsed))
}

fn builtin_read_i64(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 0, "read_i64")?;
    let word = interpreter.read_word()?;
    let parsed = word
        .parse::<i64>()
        .map_err(|_| RuntimeError::message(format!("read_i64 could not parse '{word}' as i64")))?;
    Ok(Value::I64(parsed))
}

fn builtin_read_f64(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 0, "read_f64")?;
    let word = interpreter.read_word()?;
    let parsed = word
        .parse::<f64>()
        .map_err(|_| RuntimeError::message(format!("read_f64 could not parse '{word}' as f64")))?;
    Ok(Value::F64(parsed))
}

fn builtin_read_word(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 0, "read_word")?;
    Ok(Value::Str(interpreter.read_word()?))
}

fn builtin_read_all(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 0, "read_all")?;
    Ok(Value::Str(interpreter.read_all()))
}

fn builtin_read_ints(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "read_ints")?;
    let count = expect_i32(&args[0], "read_ints")?.max(0) as usize;
    let mut values = Vec::with_capacity(count);
    for _ in 0..count {
        values.push(builtin_read_int(interpreter, Vec::new())?);
    }
    Ok(Value::Vec(values))
}

fn builtin_read_i64s(
    interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "read_i64s")?;
    let count = expect_i32(&args[0], "read_i64s")?.max(0) as usize;
    let mut values = Vec::with_capacity(count);
    for _ in 0..count {
        values.push(builtin_read_i64(interpreter, Vec::new())?);
    }
    Ok(Value::Vec(values))
}

fn shell_command(command: &str) -> Command {
    if cfg!(windows) {
        let mut cmd = Command::new("cmd");
        cmd.args(["/C", command]);
        cmd
    } else {
        let mut cmd = Command::new("sh");
        cmd.args(["-lc", command]);
        cmd
    }
}

fn builtin_fs_read(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "read")?;
    let path = expect_string(&args[0], "read")?;
    let content =
        fs::read_to_string(&path).map_err(|error| RuntimeError::message(format!("read failed: {error}")))?;
    Ok(Value::Str(content))
}

fn builtin_fs_write(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "write")?;
    let path = expect_string(&args[0], "write")?;
    let content = expect_string(&args[1], "write")?;
    fs::write(&path, content).map_err(|error| RuntimeError::message(format!("write failed: {error}")))?;
    Ok(Value::Bool(true))
}

fn builtin_fs_exists(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "exists")?;
    let path = expect_string(&args[0], "exists")?;
    Ok(Value::Bool(PathBuf::from(path).exists()))
}

fn builtin_fs_mkdir(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "mkdir")?;
    let path = expect_string(&args[0], "mkdir")?;
    fs::create_dir_all(&path).map_err(|error| RuntimeError::message(format!("mkdir failed: {error}")))?;
    Ok(Value::Bool(true))
}

fn builtin_fs_read_dir(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "read_dir")?;
    let path = expect_string(&args[0], "read_dir")?;
    let entries =
        fs::read_dir(&path).map_err(|error| RuntimeError::message(format!("read_dir failed: {error}")))?;
    let mut values = Vec::new();
    for entry in entries {
        let entry = entry.map_err(|error| RuntimeError::message(format!("read_dir failed: {error}")))?;
        let name = entry.file_name().to_string_lossy().to_string();
        if name != "." && name != ".." {
            values.push(Value::Str(name));
        }
    }
    Ok(Value::Vec(values))
}

fn builtin_fs_remove(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "remove")?;
    let path = expect_string(&args[0], "remove")?;
    let meta =
        fs::metadata(&path).map_err(|error| RuntimeError::message(format!("remove failed: {error}")))?;
    if meta.is_dir() {
        fs::remove_dir_all(&path)
            .map_err(|error| RuntimeError::message(format!("remove failed: {error}")))?;
    } else {
        fs::remove_file(&path).map_err(|error| RuntimeError::message(format!("remove failed: {error}")))?;
    }
    Ok(Value::Bool(true))
}

fn builtin_fs_getcwd(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 0, "getcwd")?;
    let cwd = env::current_dir()
        .map_err(|error| RuntimeError::message(format!("getcwd failed: {error}")))?;
    Ok(Value::Str(cwd.to_string_lossy().to_string()))
}

fn builtin_fs_path_join(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 2, "path_join")?;
    let left = expect_string(&args[0], "path_join")?;
    let right = expect_string(&args[1], "path_join")?;
    let mut path = PathBuf::from(left);
    path.push(right);
    Ok(Value::Str(path.to_string_lossy().replace('\\', "/")))
}

fn builtin_fs_is_dir(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "is_dir")?;
    let path = expect_string(&args[0], "is_dir")?;
    Ok(Value::Bool(
        fs::metadata(&path).map(|meta| meta.is_dir()).unwrap_or(false),
    ))
}

fn builtin_fs_filesize(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "filesize")?;
    let path = expect_string(&args[0], "filesize")?;
    let size = fs::metadata(&path)
        .map_err(|error| RuntimeError::message(format!("filesize failed: {error}")))?
        .len();
    let size = i64::try_from(size).map_err(|_| RuntimeError::message("filesize overflowed i64 range"))?;
    Ok(Value::I64(size))
}

fn builtin_process_run(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "run")?;
    let command = expect_string(&args[0], "run")?;
    let status = shell_command(&command)
        .status()
        .map_err(|error| RuntimeError::message(format!("run failed: {error}")))?;
    Ok(Value::I32(status.code().unwrap_or(1)))
}

fn builtin_process_capture(
    _: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "capture")?;
    let command = expect_string(&args[0], "capture")?;
    let output = shell_command(&command)
        .output()
        .map_err(|error| RuntimeError::message(format!("capture failed: {error}")))?;
    Ok(Value::Str(
        String::from_utf8_lossy(&output.stdout).replace("\r\n", "\n"),
    ))
}

fn builtin_process_argv(
    _interpreter: &mut Interpreter<'_>,
    args: Vec<Value>,
) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "argv")?;
    let index = expect_i32(&args[0], "argv")?;
    let argv = env::args().collect::<Vec<_>>();
    let Some(value) = argv.get(index.max(0) as usize) else {
        return Ok(Value::Str(String::new()));
    };
    Ok(Value::Str(value.clone()))
}

fn builtin_process_argc(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 0, "argc")?;
    Ok(Value::I32(env::args().count() as i32))
}

fn builtin_process_env(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "env")?;
    let name = expect_string(&args[0], "env")?;
    Ok(Value::Str(env::var(name).unwrap_or_default()))
}

fn builtin_process_exit(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "exit")?;
    let code = expect_i32(&args[0], "exit")?;
    Err(RuntimeError::message(format!(
        "exit({code}) is not supported in interpreter mode"
    )))
}

#[cfg(test)]
mod tests {
    use super::StdlibRegistry;
    use crate::{Interpreter, RuntimeError, SymbolTable, Value};
    use std::fs;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn unique_temp_dir(prefix: &str) -> std::path::PathBuf {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("time")
            .as_nanos();
        let dir = std::env::temp_dir().join(format!("{prefix}-{nanos}"));
        fs::create_dir_all(&dir).expect("create temp dir");
        dir
    }

    #[test]
    fn from_int_accepts_i32_values() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let handler = registry.handler("from_int").expect("from_int handler");
        let result = handler(&mut interpreter, vec![Value::I32(42)]).expect("from_int result");
        assert_eq!(result, Value::Str(String::from("42")));
    }

    #[test]
    fn print_handlers_render_without_intermediate_type_errors() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let println_handler = registry.handler("println").expect("println handler");
        println_handler(&mut interpreter, vec![Value::I32(7)]).expect("println result");
        println_handler(&mut interpreter, vec![Value::Bool(true)]).expect("println result");
        assert_eq!(interpreter.stdout(), "7\ntrue\n");
    }

    #[test]
    fn to_bool_matches_runtime_surface() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let handler = registry.handler("to_bool").expect("to_bool handler");

        let true_value = handler(&mut interpreter, vec![Value::Str(String::from("true"))])
            .expect("to_bool true");
        let one_value =
            handler(&mut interpreter, vec![Value::Str(String::from("1"))]).expect("to_bool one");
        let yes_value =
            handler(&mut interpreter, vec![Value::Str(String::from("yes"))]).expect("to_bool yes");

        assert_eq!(true_value, Value::Bool(true));
        assert_eq!(one_value, Value::Bool(true));
        assert_eq!(yes_value, Value::Bool(false));
    }

    #[test]
    fn len_uses_runtime_byte_count_for_strings() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let handler = registry.handler("len").expect("len handler");

        let result =
            handler(&mut interpreter, vec![Value::Str(String::from("hé"))]).expect("len result");

        assert_eq!(result, Value::I32(3));
    }

    #[test]
    fn pad_left_and_right_follow_runtime_width_rules() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let pad_left = registry.handler("pad_left").expect("pad_left handler");
        let pad_right = registry.handler("pad_right").expect("pad_right handler");

        let left = pad_left(
            &mut interpreter,
            vec![
                Value::Str(String::from("é")),
                Value::I32(4),
                Value::Str(String::from(" ")),
            ],
        )
        .expect("pad_left result");
        let right = pad_right(
            &mut interpreter,
            vec![
                Value::Str(String::from("é")),
                Value::I32(4),
                Value::Str(String::from(" ")),
            ],
        )
        .expect("pad_right result");

        assert_eq!(left, Value::Str(String::from("  é")));
        assert_eq!(right, Value::Str(String::from("é  ")));
    }

    #[test]
    fn strip_removes_all_requested_edge_characters() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let handler = registry.handler("strip").expect("strip handler");

        let result = handler(
            &mut interpreter,
            vec![
                Value::Str(String::from("--hello__")),
                Value::Str(String::from("-_")),
            ],
        )
        .expect("strip result");

        assert_eq!(result, Value::Str(String::from("hello")));
    }

    #[test]
    fn concat_requires_string_arguments() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let handler = registry.handler("concat").expect("concat handler");

        let error = handler(&mut interpreter, vec![Value::I32(1), Value::Str(String::from("x"))])
            .expect_err("concat should reject non-string");

        assert!(matches!(error, RuntimeError::Message(message) if message.contains("concat expected str")));
    }

    #[test]
    fn join_requires_array_of_strings() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let handler = registry.handler("join").expect("join handler");

        let error = handler(
            &mut interpreter,
            vec![
                Value::Vec(vec![Value::Str(String::from("a")), Value::I32(2)]),
                Value::Str(String::from(",")),
            ],
        )
        .expect_err("join should reject non-string elements");

        assert!(matches!(error, RuntimeError::Message(message) if message.contains("join expected vec[str]")));
    }

    #[test]
    fn replace_empty_old_value_matches_runtime_surface() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let handler = registry.handler("replace").expect("replace handler");

        let result = handler(
            &mut interpreter,
            vec![
                Value::Str(String::from("abc")),
                Value::Str(String::new()),
                Value::Str(String::from("x")),
            ],
        )
        .expect("replace result");

        assert_eq!(result, Value::Str(String::from("abc")));
    }

    #[test]
    fn split_empty_separator_avoids_boundary_empty_strings() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let handler = registry.handler("split").expect("split handler");

        let result = handler(
            &mut interpreter,
            vec![Value::Str(String::from("abc")), Value::Str(String::new())],
        )
        .expect("split result");

        assert_eq!(
            result,
            Value::Vec(vec![
                Value::Str(String::from("a")),
                Value::Str(String::from("b")),
                Value::Str(String::from("c")),
            ])
        );
    }

    #[test]
    fn char_at_negative_index_returns_empty_string() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let handler = registry.handler("char_at").expect("char_at handler");

        let result = handler(
            &mut interpreter,
            vec![Value::Str(String::from("abc")), Value::I32(-1)],
        )
        .expect("char_at result");

        assert_eq!(result, Value::Str(String::new()));
    }

    #[test]
    fn to_int_matches_runtime_parse_fallbacks() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let handler = registry.handler("to_int").expect("to_int handler");

        let invalid = handler(&mut interpreter, vec![Value::Str(String::from("abc"))])
            .expect("invalid to_int result");
        let prefix = handler(&mut interpreter, vec![Value::Str(String::from("  -34ms"))])
            .expect("prefix to_int result");

        assert_eq!(invalid, Value::I32(0));
        assert_eq!(prefix, Value::I32(-34));
    }

    #[test]
    fn to_f64_matches_runtime_parse_fallbacks() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let handler = registry.handler("to_f64").expect("to_f64 handler");

        let invalid = handler(&mut interpreter, vec![Value::Str(String::from("abc"))])
            .expect("invalid to_f64 result");
        let prefix = handler(
            &mut interpreter,
            vec![Value::Str(String::from("  -2.5e2ms"))],
        )
        .expect("prefix to_f64 result");

        assert_eq!(invalid, Value::F64(0.0));
        assert_eq!(prefix, Value::F64(-250.0));
    }

    #[test]
    fn time_module_exports_runtime_clock_and_sleep() {
        let registry = StdlibRegistry::new();
        let exports = registry.module_exports("time").expect("time exports");
        assert!(exports.iter().any(|name| name == "now_ms"));
        assert!(exports.iter().any(|name| name == "sleep_ms"));
    }

    #[test]
    fn now_ms_and_sleep_ms_progress_time() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let now_ms = registry.handler("now_ms").expect("now_ms handler");
        let sleep_ms = registry.handler("sleep_ms").expect("sleep_ms handler");

        let start = now_ms(&mut interpreter, Vec::new()).expect("start");
        sleep_ms(&mut interpreter, vec![Value::I64(20)]).expect("sleep");
        let end = now_ms(&mut interpreter, Vec::new()).expect("end");

        let start = match start {
            Value::I64(value) => value,
            other => panic!("unexpected now_ms value: {other:?}"),
        };
        let end = match end {
            Value::I64(value) => value,
            other => panic!("unexpected now_ms value: {other:?}"),
        };

        assert!(end >= start);
        assert!(end - start >= 10);
    }

    #[test]
    fn fs_module_exports_expected_helpers() {
        let registry = StdlibRegistry::new();
        let exports = registry.module_exports("fs").expect("fs exports");
        assert!(exports.iter().any(|name| name == "read"));
        assert!(exports.iter().any(|name| name == "write"));
        assert!(exports.iter().any(|name| name == "read_dir"));
        assert!(exports.iter().any(|name| name == "path_join"));
    }

    #[test]
    fn fs_handlers_round_trip_files() {
        let registry = StdlibRegistry::new();
        let mut interpreter = Interpreter::new(SymbolTable::default());
        let write = registry.handler("write").expect("write handler");
        let read = registry.handler("read").expect("read handler");
        let exists = registry.handler("exists").expect("exists handler");
        let getcwd = registry.handler("getcwd").expect("getcwd handler");
        let path_join = registry.handler("path_join").expect("path_join handler");
        let read_dir = registry.handler("read_dir").expect("read_dir handler");
        let filesize = registry.handler("filesize").expect("filesize handler");

        let dir = unique_temp_dir("thagore-stdlib-fs");
        let file = dir.join("probe.txt");
        write(
            &mut interpreter,
            vec![
                Value::Str(file.to_string_lossy().to_string()),
                Value::Str(String::from("probe")),
            ],
        )
        .expect("write result");

        assert_eq!(
            exists(
                &mut interpreter,
                vec![Value::Str(file.to_string_lossy().to_string())],
            )
            .expect("exists result"),
            Value::Bool(true)
        );
        assert_eq!(
            read(
                &mut interpreter,
                vec![Value::Str(file.to_string_lossy().to_string())],
            )
            .expect("read result"),
            Value::Str(String::from("probe"))
        );
        match filesize(
            &mut interpreter,
            vec![Value::Str(file.to_string_lossy().to_string())],
        )
        .expect("filesize result")
        {
            Value::I64(size) => assert!(size >= 5),
            other => panic!("unexpected filesize value: {other:?}"),
        }
        let cwd = getcwd(&mut interpreter, Vec::new()).expect("getcwd result");
        assert!(matches!(cwd, Value::Str(_)));
        let joined = path_join(
            &mut interpreter,
            vec![
                Value::Str(dir.to_string_lossy().to_string()),
                Value::Str(String::from("probe.txt")),
            ],
        )
        .expect("path_join result");
        assert_eq!(joined, Value::Str(file.to_string_lossy().replace('\\', "/")));
        let dir_values = read_dir(
            &mut interpreter,
            vec![Value::Str(dir.to_string_lossy().to_string())],
        )
        .expect("read_dir result");
        assert!(matches!(dir_values, Value::Vec(values) if values.contains(&Value::Str(String::from("probe.txt")))));

        fs::remove_dir_all(dir).expect("cleanup dir");
    }
}
