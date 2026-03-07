//! Return-collection helpers for implicit function return inference.

extern crate alloc;

use alloc::vec::Vec;

use thagore_ast::{BlockRef, ExprRef, Span, Stmt, StmtRef};

/// One explicit `return` observed inside a function body.
#[derive(Debug, Clone, Copy, PartialEq)]
pub(crate) struct ReturnSite<'ast> {
    /// Returned expression, when present.
    pub value: Option<ExprRef<'ast>>,
    /// Span of the return statement.
    pub span: Span,
}

/// Collects all explicit `return` sites contained in `block`.
pub(crate) fn collect_return_sites<'ast>(
    block: BlockRef<'ast>,
    returns: &mut Vec<ReturnSite<'ast>>,
) {
    for stmt in block.statements {
        collect_stmt_returns(stmt, returns);
    }
}

/// Returns `true` when control flow cannot fall through the end of `block`.
pub(crate) fn block_guarantees_return(block: BlockRef<'_>) -> bool {
    let Some(last) = block.statements.last() else {
        return false;
    };
    stmt_guarantees_return(last)
}

fn collect_stmt_returns<'ast>(stmt: StmtRef<'ast>, returns: &mut Vec<ReturnSite<'ast>>) {
    match stmt {
        Stmt::Return(node) => returns.push(ReturnSite {
            value: node.value,
            span: node.span,
        }),
        Stmt::If(node) => {
            collect_return_sites(node.then_block, returns);
            if let Some(else_block) = node.else_block {
                collect_return_sites(else_block, returns);
            }
        }
        Stmt::While(node) => collect_return_sites(node.body, returns),
        Stmt::For(node) => collect_return_sites(node.body, returns),
        Stmt::Let(_) | Stmt::Expr(_) | Stmt::Break(_) | Stmt::Continue(_) => {}
    }
}

fn stmt_guarantees_return(stmt: StmtRef<'_>) -> bool {
    match stmt {
        Stmt::Return(_) => true,
        Stmt::If(node) => node
            .else_block
            .is_some_and(|else_block| block_guarantees_return(node.then_block) && block_guarantees_return(else_block)),
        Stmt::While(_) | Stmt::For(_) | Stmt::Let(_) | Stmt::Expr(_) | Stmt::Break(_) | Stmt::Continue(_) => false,
    }
}
