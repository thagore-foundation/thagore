//! Interpreter-side standard library implemented in pure Rust.

use std::collections::HashMap;

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
        registry.register("read_line", builtin_read_line);
        registry.register("read_int", builtin_read_int);
        registry.register("read_i64", builtin_read_i64);
        registry.register("read_f64", builtin_read_f64);
        registry.register("read_word", builtin_read_word);
        registry.register("read_all", builtin_read_all);
        registry.register("read_ints", builtin_read_ints);
        registry.register("read_i64s", builtin_read_i64s);

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

fn expect_f64(value: &Value, name: &str) -> Result<f64, RuntimeError> {
    match value {
        Value::F64(number) => Ok(*number),
        other => Err(RuntimeError::message(format!(
            "{name} expected f64, found {}",
            other.type_name()
        ))),
    }
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
    let parsed = text
        .trim()
        .parse::<i32>()
        .map_err(|_| RuntimeError::message(format!("to_int could not parse '{text}' as i32")))?;
    Ok(Value::I32(parsed))
}

fn builtin_to_f64(_: &mut Interpreter<'_>, args: Vec<Value>) -> Result<Value, RuntimeError> {
    expect_arity(&args, 1, "to_f64")?;
    let text = expect_string(&args[0], "to_f64")?;
    let parsed = text
        .trim()
        .parse::<f64>()
        .map_err(|_| RuntimeError::message(format!("to_f64 could not parse '{text}' as f64")))?;
    Ok(Value::F64(parsed))
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
        return Err(RuntimeError::message("char_at index must be non-negative"));
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

#[cfg(test)]
mod tests {
    use super::StdlibRegistry;
    use crate::{Interpreter, RuntimeError, SymbolTable, Value};

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
}
