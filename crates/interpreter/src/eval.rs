//! Expression and statement evaluation logic for the Thagore interpreter.

use thagore_ast::{
    AssignExpr, BinOp, BinaryExpr, BlockRef, CallExpr, Decl, Expr, ExprRef, FieldAccessExpr,
    ForStmt, IdentExpr, IfStmt, ImportDecl, IndexExpr, LetDecl, LitExpr, Literal, ReturnStmt,
    Stmt, StmtRef, StructDecl, UnaryExpr, UnaryOp, WhileStmt,
};

use crate::value::Value;
use crate::{Interpreter, RuntimeError};

impl<'ast> Interpreter<'ast> {
    pub(crate) fn collect_declarations(
        &mut self,
        decls: &'ast [Decl<'ast>],
    ) -> Result<(), RuntimeError> {
        for decl in decls {
            match decl {
                Decl::Func(func) => {
                    let name = self.name(func.name)?;
                    self.functions.insert(name, func);
                }
                Decl::GenericFunc(_) | Decl::GenericStruct(_) | Decl::GenericImpl(_) => {
                    return Err(RuntimeError::unsupported(
                        "generic declarations are not yet supported in the playground interpreter",
                    ));
                }
                Decl::Import(import_decl) => self.register_import(import_decl)?,
                Decl::Const(const_decl) => {
                    let name = self.name(const_decl.name)?;
                    let value = self.eval_expr(const_decl.value)?;
                    self.env.define(name, value);
                }
                Decl::Let(_) => {
                    return Err(RuntimeError::unsupported(
                        "top-level let declarations are not supported in the playground interpreter",
                    ));
                }
                Decl::Struct(struct_decl) => self.register_struct(struct_decl)?,
                Decl::Extern(_) => {
                    return Err(RuntimeError::unsupported(
                        "extern declarations are not supported in the playground interpreter",
                    ));
                }
                Decl::Impl(_) => {
                    return Err(RuntimeError::unsupported(
                        "impl blocks are not supported in the playground interpreter",
                    ));
                }
                Decl::Intent(_) => {
                    return Err(RuntimeError::unsupported(
                        "intent declarations are not supported in the playground interpreter",
                    ));
                }
                Decl::Flow(_) => {
                    return Err(RuntimeError::unsupported(
                        "flow declarations are not supported in the playground interpreter",
                    ));
                }
            }
        }
        Ok(())
    }

    fn register_struct(&mut self, decl: &'ast StructDecl<'ast>) -> Result<(), RuntimeError> {
        let name = self.name(decl.name)?;
        self.struct_names.push(name);
        Ok(())
    }

    fn register_import(&mut self, decl: &'ast ImportDecl<'ast>) -> Result<(), RuntimeError> {
        if decl.relative_level > 0 {
            return Err(RuntimeError::unsupported(
                "relative imports are not supported in the browser playground",
            ));
        }

        let module = decl
            .path_segments
            .iter()
            .map(|segment| self.name(*segment))
            .collect::<Result<Vec<_>, _>>()?
            .join(".");
        let module_key = module
            .split('.')
            .next_back()
            .ok_or_else(|| RuntimeError::message("empty import path"))?
            .to_string();
        if self.stdlib.module_exports(&module_key).is_none() {
            return Err(RuntimeError::unsupported(format!(
                "module '{module_key}' is not available in the browser playground"
            )));
        }

        if decl.is_from {
            for symbol in decl.symbols.iter() {
                let export_name = self.name(symbol.name)?;
                let local_name = match symbol.alias {
                    Some(alias) => self.name(alias)?,
                    None => export_name.clone(),
                };
                if let Some(resolved) = self.stdlib.resolve_module_symbol(&module_key, &export_name) {
                    self.env.define(local_name, Value::Callable(resolved));
                }
            }
        } else if decl.include_all {
            if let Some(exports) = self.stdlib.module_exports(&module_key) {
                for symbol in exports {
                    self.env
                        .define(symbol.clone(), Value::Callable(symbol.clone()));
                }
            }
        } else {
            let alias = match decl.alias {
                Some(alias) => self.name(alias)?,
                None => module_key,
            };
            self.env.define(alias, Value::Module(module));
        }
        Ok(())
    }

    pub(crate) fn exec_block(&mut self, block: BlockRef<'ast>) -> Result<Value, RuntimeError> {
        self.env.push();
        let mut last = Value::Void;
        for stmt in block.statements.iter() {
            last = self.exec_stmt(stmt)?;
            if last.is_control_flow() {
                self.env.pop();
                return Ok(last);
            }
        }
        self.env.pop();
        Ok(last)
    }

    pub(crate) fn exec_stmt(&mut self, stmt: StmtRef<'ast>) -> Result<Value, RuntimeError> {
        self.tick()?;
        match stmt {
            Stmt::Let(decl) => self.exec_let(decl),
            Stmt::Expr(expr) => self.eval_expr(expr.expr),
            Stmt::Return(stmt) => self.exec_return(stmt),
            Stmt::If(stmt) => self.exec_if(stmt),
            Stmt::While(stmt) => self.exec_while(stmt),
            Stmt::For(stmt) => self.exec_for(stmt),
            Stmt::Break(_) => Ok(Value::Break),
            Stmt::Continue(_) => Ok(Value::Continue),
        }
    }

    fn exec_let(&mut self, decl: &'ast LetDecl<'ast>) -> Result<Value, RuntimeError> {
        let name = self.name(decl.name)?;
        let value = self.eval_expr(decl.initializer)?;
        self.env.define(name, value);
        Ok(Value::Void)
    }

    fn exec_return(&mut self, stmt: &'ast ReturnStmt<'ast>) -> Result<Value, RuntimeError> {
        let value = match stmt.value {
            Some(expr) => self.eval_expr(expr)?,
            None => Value::Void,
        };
        Ok(Value::Return(Box::new(value)))
    }

    fn exec_if(&mut self, stmt: &'ast IfStmt<'ast>) -> Result<Value, RuntimeError> {
        if self.eval_expr(stmt.condition)?.as_bool()? {
            self.exec_block(stmt.then_block)
        } else if let Some(else_block) = stmt.else_block {
            self.exec_block(else_block)
        } else {
            Ok(Value::Void)
        }
    }

    fn exec_while(&mut self, stmt: &'ast WhileStmt<'ast>) -> Result<Value, RuntimeError> {
        loop {
            if !self.eval_expr(stmt.condition)?.as_bool()? {
                return Ok(Value::Void);
            }
            match self.exec_block(stmt.body)? {
                Value::Void => {}
                Value::Continue => continue,
                Value::Break => return Ok(Value::Void),
                Value::Return(value) => return Ok(Value::Return(value)),
                _ => {}
            }
        }
    }

    fn exec_for(&mut self, stmt: &'ast ForStmt<'ast>) -> Result<Value, RuntimeError> {
        let iterable = self.eval_expr(stmt.iterator)?;
        let items = match iterable {
            Value::Vec(values) => values,
            other => {
                return Err(RuntimeError::message(format!(
                    "for loop expected iterable vec, found {}",
                    other.type_name()
                )))
            }
        };
        let binding = self.name(stmt.binding)?;
        for item in items {
            self.env.push();
            self.env.define(binding.clone(), item);
            match self.exec_block(stmt.body)? {
                Value::Void => {}
                Value::Continue => {
                    self.env.pop();
                    continue;
                }
                Value::Break => {
                    self.env.pop();
                    return Ok(Value::Void);
                }
                Value::Return(value) => {
                    self.env.pop();
                    return Ok(Value::Return(value));
                }
                _ => {}
            }
            self.env.pop();
        }
        Ok(Value::Void)
    }

    pub(crate) fn eval_expr(&mut self, expr: ExprRef<'ast>) -> Result<Value, RuntimeError> {
        self.tick()?;
        match expr {
            Expr::Binary(node) => self.eval_binary(node),
            Expr::Unary(node) => self.eval_unary(node),
            Expr::Call(node) => self.eval_call(node),
            Expr::FieldAccess(node) => self.eval_field_access(node),
            Expr::Index(node) => self.eval_index(node),
            Expr::Ident(node) => self.eval_ident(node),
            Expr::Literal(node) => self.eval_literal(node),
            Expr::Assign(node) => self.eval_assign(node),
        }
    }

    fn eval_ident(&mut self, expr: &'ast IdentExpr) -> Result<Value, RuntimeError> {
        let name = self.name(expr.name)?;
        if let Some(value) = self.env.get(&name) {
            return Ok(value);
        }
        if self.functions.contains_key(&name) || self.stdlib.has_callable(&name) {
            return Ok(Value::Callable(name));
        }
        Err(RuntimeError::message(format!("unknown identifier '{name}'")))
    }

    fn eval_literal(&self, expr: &'ast LitExpr) -> Result<Value, RuntimeError> {
        match &expr.literal {
            Literal::Int(value) => {
                if let Ok(number) = i32::try_from(*value) {
                    Ok(Value::I32(number))
                } else {
                    Ok(Value::I64(*value))
                }
            }
            Literal::Float(value) => Ok(Value::F64(*value)),
            Literal::Bool(value) => Ok(Value::Bool(*value)),
            Literal::Str(symbol) => Ok(Value::Str(unescape_string(&self.name(*symbol)?))),
        }
    }

    fn eval_assign(&mut self, expr: &'ast AssignExpr<'ast>) -> Result<Value, RuntimeError> {
        let value = self.eval_expr(expr.value)?;
        match expr.target {
            Expr::Ident(node) => {
                let name = self.name(node.name)?;
                if self.env.assign(&name, value.clone()) {
                    Ok(value)
                } else {
                    Err(RuntimeError::message(format!(
                        "cannot assign to undefined variable '{name}'"
                    )))
                }
            }
            _ => Err(RuntimeError::unsupported(
                "the playground interpreter only supports assignment to variables",
            )),
        }
    }

    fn eval_unary(&mut self, expr: &'ast UnaryExpr<'ast>) -> Result<Value, RuntimeError> {
        let operand = self.eval_expr(expr.operand)?;
        match expr.op {
            UnaryOp::Plus => Ok(operand),
            UnaryOp::Neg => match operand {
                Value::I32(value) => Ok(Value::I32(-value)),
                Value::I64(value) => Ok(Value::I64(-value)),
                Value::F64(value) => Ok(Value::F64(-value)),
                other => Err(RuntimeError::message(format!(
                    "cannot negate {}",
                    other.type_name()
                ))),
            },
            UnaryOp::Not => Ok(Value::Bool(!operand.as_bool()?)),
        }
    }

    fn eval_binary(&mut self, expr: &'ast BinaryExpr<'ast>) -> Result<Value, RuntimeError> {
        match expr.op {
            BinOp::And => {
                let left = self.eval_expr(expr.left)?.as_bool()?;
                if !left {
                    return Ok(Value::Bool(false));
                }
                return Ok(Value::Bool(self.eval_expr(expr.right)?.as_bool()?));
            }
            BinOp::Or => {
                let left = self.eval_expr(expr.left)?.as_bool()?;
                if left {
                    return Ok(Value::Bool(true));
                }
                return Ok(Value::Bool(self.eval_expr(expr.right)?.as_bool()?));
            }
            _ => {}
        }

        let left = self.eval_expr(expr.left)?;
        let right = self.eval_expr(expr.right)?;
        match expr.op {
            BinOp::Add => eval_add(left, right),
            BinOp::Sub => eval_sub(left, right),
            BinOp::Mul => eval_mul(left, right),
            BinOp::Div => eval_div(left, right),
            BinOp::Rem => eval_rem(left, right),
            BinOp::Eq => Ok(Value::Bool(left == right)),
            BinOp::NotEq => Ok(Value::Bool(left != right)),
            BinOp::Lt => eval_cmp(left, right, |ordering| ordering.is_lt()),
            BinOp::LtEq => eval_cmp(left, right, |ordering| ordering.is_le()),
            BinOp::Gt => eval_cmp(left, right, |ordering| ordering.is_gt()),
            BinOp::GtEq => eval_cmp(left, right, |ordering| ordering.is_ge()),
            BinOp::And | BinOp::Or => unreachable!("handled above"),
        }
    }

    fn eval_call(&mut self, expr: &'ast CallExpr<'ast>) -> Result<Value, RuntimeError> {
        let callee = self.eval_expr(expr.callee)?;
        let callable = match callee {
            Value::Callable(name) => name,
            other => {
                return Err(RuntimeError::message(format!(
                    "{} is not callable",
                    other.type_name()
                )))
            }
        };
        let args = expr
            .args
            .iter()
            .map(|arg| self.eval_expr(*arg))
            .collect::<Result<Vec<_>, _>>()?;
        self.call_func(&callable, args)
    }

    fn eval_field_access(&mut self, expr: &'ast FieldAccessExpr<'ast>) -> Result<Value, RuntimeError> {
        let object = self.eval_expr(expr.object)?;
        let field = self.name(expr.field)?;
        match object {
            Value::Module(module) => {
                let module_key = module
                    .split('.')
                    .next_back()
                    .ok_or_else(|| RuntimeError::message("invalid module name"))?;
                if let Some(name) = self.stdlib.resolve_module_symbol(module_key, &field) {
                    Ok(Value::Callable(name))
                } else {
                    Err(RuntimeError::message(format!(
                        "module '{module_key}' has no export '{field}'"
                    )))
                }
            }
            Value::Struct { fields, .. } => fields
                .get(&field)
                .cloned()
                .ok_or_else(|| RuntimeError::message(format!("unknown field '{field}'"))),
            other => Err(RuntimeError::message(format!(
                "cannot access field '{field}' on {}",
                other.type_name()
            ))),
        }
    }

    fn eval_index(&mut self, expr: &'ast IndexExpr<'ast>) -> Result<Value, RuntimeError> {
        let object = self.eval_expr(expr.object)?;
        let index = self.eval_expr(expr.index)?;
        let index = match index {
            Value::I32(value) => usize::try_from(value)
                .map_err(|_| RuntimeError::message("index must be non-negative"))?,
            Value::I64(value) => usize::try_from(value)
                .map_err(|_| RuntimeError::message("index must be non-negative"))?,
            other => {
                return Err(RuntimeError::message(format!(
                    "index expected integer, found {}",
                    other.type_name()
                )))
            }
        };
        match object {
            Value::Vec(values) => values
                .get(index)
                .cloned()
                .ok_or_else(|| RuntimeError::message(format!("index {index} out of bounds"))),
            Value::Str(text) => Ok(Value::Str(
                text.chars().nth(index).map(|ch| ch.to_string()).unwrap_or_default(),
            )),
            other => Err(RuntimeError::message(format!(
                "cannot index {}",
                other.type_name()
            ))),
        }
    }

    pub(crate) fn call_func(&mut self, name: &str, args: Vec<Value>) -> Result<Value, RuntimeError> {
        if let Some(handler) = self.stdlib.handler(name) {
            return handler(self, args);
        }

        let func = self
            .functions
            .get(name)
            .copied()
            .ok_or_else(|| RuntimeError::message(format!("unknown function '{name}'")))?;
        self.call_depth += 1;
        if self.call_depth > self.max_call_depth {
            self.call_depth -= 1;
            return Err(RuntimeError::StackOverflow);
        }

        self.env.push();
        for (param, arg) in func.params.iter().zip(args) {
            let param_name = self.name(param.name)?;
            self.env.define(param_name, arg);
        }
        let result = match self.exec_block(func.body)? {
            Value::Return(value) => *value,
            Value::Void => Value::Void,
            other => other,
        };
        self.env.pop();
        self.call_depth = self.call_depth.saturating_sub(1);
        Ok(result)
    }
}

fn eval_add(left: Value, right: Value) -> Result<Value, RuntimeError> {
    match (left, right) {
        (Value::I32(a), Value::I32(b)) => Ok(Value::I32(a + b)),
        (Value::I64(a), Value::I64(b)) => Ok(Value::I64(a + b)),
        (Value::F64(a), Value::F64(b)) => Ok(Value::F64(a + b)),
        (Value::Str(a), Value::Str(b)) => Ok(Value::Str(format!("{a}{b}"))),
        (left, right) => Err(RuntimeError::message(format!(
            "cannot add {} and {}",
            left.type_name(),
            right.type_name()
        ))),
    }
}

fn eval_sub(left: Value, right: Value) -> Result<Value, RuntimeError> {
    match (left, right) {
        (Value::I32(a), Value::I32(b)) => Ok(Value::I32(a - b)),
        (Value::I64(a), Value::I64(b)) => Ok(Value::I64(a - b)),
        (Value::F64(a), Value::F64(b)) => Ok(Value::F64(a - b)),
        (left, right) => Err(RuntimeError::message(format!(
            "cannot subtract {} and {}",
            left.type_name(),
            right.type_name()
        ))),
    }
}

fn eval_mul(left: Value, right: Value) -> Result<Value, RuntimeError> {
    match (left, right) {
        (Value::I32(a), Value::I32(b)) => Ok(Value::I32(a * b)),
        (Value::I64(a), Value::I64(b)) => Ok(Value::I64(a * b)),
        (Value::F64(a), Value::F64(b)) => Ok(Value::F64(a * b)),
        (left, right) => Err(RuntimeError::message(format!(
            "cannot multiply {} and {}",
            left.type_name(),
            right.type_name()
        ))),
    }
}

fn eval_div(left: Value, right: Value) -> Result<Value, RuntimeError> {
    match (left, right) {
        (Value::I32(_), Value::I32(0))
        | (Value::I64(_), Value::I64(0))
        | (Value::F64(_), Value::F64(0.0)) => Err(RuntimeError::DivisionByZero),
        (Value::I32(a), Value::I32(b)) => Ok(Value::I32(a / b)),
        (Value::I64(a), Value::I64(b)) => Ok(Value::I64(a / b)),
        (Value::F64(a), Value::F64(b)) => Ok(Value::F64(a / b)),
        (left, right) => Err(RuntimeError::message(format!(
            "cannot divide {} and {}",
            left.type_name(),
            right.type_name()
        ))),
    }
}

fn eval_rem(left: Value, right: Value) -> Result<Value, RuntimeError> {
    match (left, right) {
        (Value::I32(_), Value::I32(0)) | (Value::I64(_), Value::I64(0)) => {
            Err(RuntimeError::DivisionByZero)
        }
        (Value::I32(a), Value::I32(b)) => Ok(Value::I32(a % b)),
        (Value::I64(a), Value::I64(b)) => Ok(Value::I64(a % b)),
        (Value::F64(a), Value::F64(b)) => Ok(Value::F64(a % b)),
        (left, right) => Err(RuntimeError::message(format!(
            "cannot compute remainder of {} and {}",
            left.type_name(),
            right.type_name()
        ))),
    }
}

fn eval_cmp(left: Value, right: Value, predicate: impl Fn(std::cmp::Ordering) -> bool) -> Result<Value, RuntimeError> {
    let result = match (left, right) {
        (Value::I32(a), Value::I32(b)) => predicate(a.cmp(&b)),
        (Value::I64(a), Value::I64(b)) => predicate(a.cmp(&b)),
        (Value::F64(a), Value::F64(b)) => predicate(
            a.partial_cmp(&b)
                .ok_or_else(|| RuntimeError::message("cannot compare NaN values"))?,
        ),
        (Value::Str(a), Value::Str(b)) => predicate(a.cmp(&b)),
        (left, right) => {
            return Err(RuntimeError::message(format!(
                "cannot compare {} and {}",
                left.type_name(),
                right.type_name()
            )))
        }
    };
    Ok(Value::Bool(result))
}

fn unescape_string(text: &str) -> String {
    let mut result = String::new();
    let mut chars = text.chars();
    while let Some(ch) = chars.next() {
        if ch != '\\' {
            result.push(ch);
            continue;
        }
        match chars.next() {
            Some('n') => result.push('\n'),
            Some('r') => result.push('\r'),
            Some('t') => result.push('\t'),
            Some('"') => result.push('"'),
            Some('\\') => result.push('\\'),
            Some(other) => {
                result.push('\\');
                result.push(other);
            }
            None => result.push('\\'),
        }
    }
    result
}

trait BoolValue {
    fn as_bool(&self) -> Result<bool, RuntimeError>;
}

impl BoolValue for Value {
    fn as_bool(&self) -> Result<bool, RuntimeError> {
        match self {
            Value::Bool(value) => Ok(*value),
            other => Err(RuntimeError::message(format!(
                "expected bool, found {}",
                other.type_name()
            ))),
        }
    }
}
