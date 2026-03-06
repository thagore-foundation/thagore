//! Validation pass for lowered Thagore IR.

extern crate alloc;

use alloc::collections::BTreeSet;
use alloc::vec::Vec;

use crate::block::{BlockId, Terminator};
use crate::error::LoweringError;
use crate::instr::{Instr, Value};
use crate::module::{IrFunction, IrModule};

/// Validates every function in a lowered IR module.
#[must_use]
pub fn validate_module(module: &IrModule) -> Vec<LoweringError> {
    let mut errors = Vec::new();
    for function in &module.functions {
        errors.extend(validate_function(function));
    }
    errors
}

/// Validates a single lowered IR function.
#[must_use]
pub fn validate_function(function: &IrFunction) -> Vec<LoweringError> {
    if function.is_extern {
        return Vec::new();
    }

    let mut errors = Vec::new();
    let valid_blocks: BTreeSet<BlockId> = function.blocks.iter().map(|block| block.id).collect();
    let mut all_defined = BTreeSet::new();
    for (value, _) in &function.params {
        all_defined.insert(*value);
    }

    for block in &function.blocks {
        for instr in &block.instructions {
            if let Some(value) = instr.defined_value() {
                all_defined.insert(value);
            }
        }
    }

    let mut seen = BTreeSet::new();
    for (value, _) in &function.params {
        seen.insert(*value);
    }

    for block in &function.blocks {
        if block.terminator.is_none() {
            errors.push(LoweringError::MissingTerminator {
                block: block.id.as_u32(),
            });
        }

        for successor in &block.successors {
            if !valid_blocks.contains(successor) {
                errors.push(LoweringError::InvalidBlockEdge {
                    from: block.id.as_u32(),
                    to: successor.as_u32(),
                });
            }
        }

        for predecessor in &block.predecessors {
            if !valid_blocks.contains(predecessor) {
                errors.push(LoweringError::InvalidBlockEdge {
                    from: predecessor.as_u32(),
                    to: block.id.as_u32(),
                });
            }
        }

        for instr in &block.instructions {
            match instr {
                Instr::Const(..) | Instr::Alloc(..) => {}
                Instr::BinOp(_, _, left, right) => {
                    require_seen(&mut errors, &seen, *left, block.id);
                    require_seen(&mut errors, &seen, *right, block.id);
                }
                Instr::UnOp(_, _, operand)
                | Instr::Load(_, operand)
                | Instr::Store(_, operand)
                | Instr::SetField(_, _, operand) => {
                    require_seen(&mut errors, &seen, *operand, block.id);
                    if matches!(instr, Instr::Store(_, _)) {
                        if let Instr::Store(slot, _) = instr {
                            require_seen(&mut errors, &seen, *slot, block.id);
                        }
                    } else if matches!(instr, Instr::SetField(_, _, _)) {
                        if let Instr::SetField(slot, _, _) = instr {
                            require_seen(&mut errors, &seen, *slot, block.id);
                        }
                    }
                }
                Instr::Call(_, _, args) => {
                    for value in args {
                        require_seen(&mut errors, &seen, *value, block.id);
                    }
                }
                Instr::GetField(_, object, _) => {
                    require_seen(&mut errors, &seen, *object, block.id);
                }
                Instr::Index(_, object, index) => {
                    require_seen(&mut errors, &seen, *object, block.id);
                    require_seen(&mut errors, &seen, *index, block.id);
                }
                Instr::Phi(value, inputs) => {
                    for (incoming, predecessor) in inputs {
                        if !all_defined.contains(incoming) {
                            errors.push(LoweringError::UndefinedValue {
                                value: incoming.as_u32(),
                                block: block.id.as_u32(),
                            });
                        }
                        if !block.predecessors.contains(predecessor) {
                            errors.push(LoweringError::InvalidPhiInput {
                                block: block.id.as_u32(),
                                predecessor: predecessor.as_u32(),
                                value: incoming.as_u32(),
                            });
                        }
                    }
                    seen.insert(*value);
                    continue;
                }
            }

            if let Some(value) = instr.defined_value() {
                seen.insert(value);
            }
        }

        if let Some(terminator) = &block.terminator {
            match terminator {
                Terminator::Return(Some(value)) => {
                    require_seen(&mut errors, &seen, *value, block.id)
                }
                Terminator::Jump(target) => {
                    if !valid_blocks.contains(target) {
                        errors.push(LoweringError::InvalidBlockEdge {
                            from: block.id.as_u32(),
                            to: target.as_u32(),
                        });
                    }
                }
                Terminator::Branch(condition, then_block, else_block) => {
                    require_seen(&mut errors, &seen, *condition, block.id);
                    for target in [then_block, else_block] {
                        if !valid_blocks.contains(target) {
                            errors.push(LoweringError::InvalidBlockEdge {
                                from: block.id.as_u32(),
                                to: target.as_u32(),
                            });
                        }
                    }
                }
                Terminator::Return(None) | Terminator::Unreachable => {}
            }
        }
    }

    errors
}

fn require_seen(
    errors: &mut Vec<LoweringError>,
    seen: &BTreeSet<Value>,
    value: Value,
    block: BlockId,
) {
    if !seen.contains(&value) {
        errors.push(LoweringError::UndefinedValue {
            value: value.as_u32(),
            block: block.as_u32(),
        });
    }
}
