//! LLVM type lowering for Thagore `TypeId` values.

use std::collections::BTreeMap;

use inkwell::context::Context;
use inkwell::types::{
    BasicMetadataTypeEnum, BasicType, BasicTypeEnum, FunctionType as LlvmFunctionType, PointerType,
    StructType, VoidType,
};
use inkwell::AddressSpace;
use thagore_ast::InternedStr;
use thagore_typeck::{TypeArena, TypeId, TypeKind};

use crate::error::CodegenError;

/// LLVM type mapping built once per code generation session.
#[derive(Debug)]
pub struct TypeMap<'ctx> {
    void_types: BTreeMap<TypeId, VoidType<'ctx>>,
    basic_types: BTreeMap<TypeId, BasicTypeEnum<'ctx>>,
    struct_types: BTreeMap<InternedStr, StructType<'ctx>>,
}

impl<'ctx> TypeMap<'ctx> {
    /// Builds a complete `TypeId -> LLVM type` lookup table.
    pub fn new<F>(
        context: &'ctx Context,
        arena: &TypeArena,
        mut name_of: F,
    ) -> Result<Self, Vec<CodegenError>>
    where
        F: FnMut(InternedStr) -> String,
    {
        let mut map = Self {
            void_types: BTreeMap::new(),
            basic_types: BTreeMap::new(),
            struct_types: BTreeMap::new(),
        };
        let mut errors = Vec::new();

        map.void_types.insert(arena.unit(), context.void_type());
        map.basic_types
            .insert(arena.i32(), context.i32_type().into());
        map.basic_types
            .insert(arena.i64(), context.i64_type().into());
        map.basic_types
            .insert(arena.f64(), context.f64_type().into());
        map.basic_types
            .insert(arena.bool(), context.bool_type().into());
        map.basic_types.insert(
            arena.str(),
            context
                .i8_type()
                .ptr_type(AddressSpace::default())
                .as_basic_type_enum(),
        );

        for index in 0..arena.len() {
            let type_id = TypeId::new(index as u32);
            if let TypeKind::Struct(struct_ty) = arena.kind(type_id) {
                let llvm_name = name_of(struct_ty.name);
                let opaque = context.opaque_struct_type(&llvm_name);
                map.struct_types.insert(struct_ty.name, opaque);
                map.basic_types.insert(type_id, opaque.as_basic_type_enum());
            }
        }

        for index in 0..arena.len() {
            let type_id = TypeId::new(index as u32);
            match arena.kind(type_id) {
                TypeKind::Unit => {
                    map.void_types.insert(type_id, context.void_type());
                }
                TypeKind::Unknown | TypeKind::Infer(_) | TypeKind::IntInfer(_) => {}
                TypeKind::I32 | TypeKind::I64 | TypeKind::F64 | TypeKind::Bool | TypeKind::Str => {}
                TypeKind::Struct(struct_ty) => {
                    let field_types = struct_ty
                        .fields
                        .iter()
                        .filter_map(|field| map.basic_types.get(&field.ty).copied())
                        .collect::<Vec<_>>();
                    if field_types.len() != struct_ty.fields.len() {
                        errors.push(CodegenError::MissingType {
                            ty: type_id,
                            span: None,
                        });
                        continue;
                    }
                    if let Some(llvm_struct) = map.struct_types.get(&struct_ty.name) {
                        llvm_struct.set_body(&field_types, false);
                    }
                }
                TypeKind::Array(element) => {
                    let Some(element_type) = map.basic_types.get(element).copied() else {
                        errors.push(CodegenError::MissingType {
                            ty: *element,
                            span: None,
                        });
                        continue;
                    };
                    let fat_ptr = context.struct_type(
                        &[
                            context.i64_type().as_basic_type_enum(),
                            element_type
                                .ptr_type(AddressSpace::default())
                                .as_basic_type_enum(),
                        ],
                        false,
                    );
                    map.basic_types
                        .insert(type_id, fat_ptr.as_basic_type_enum());
                }
                TypeKind::Function(signature) => {
                    let return_type =
                        match map.function_type_for_signature(context, arena, signature) {
                            Ok(Some(function_type)) => function_type,
                            Ok(None) => {
                                errors.push(CodegenError::MissingType {
                                    ty: type_id,
                                    span: None,
                                });
                                continue;
                            }
                            Err(error) => {
                                errors.push(error);
                                continue;
                            }
                        };
                    let ptr = return_type
                        .ptr_type(AddressSpace::default())
                        .as_basic_type_enum();
                    map.basic_types.insert(type_id, ptr);
                }
            }
        }

        if errors.is_empty() {
            Ok(map)
        } else {
            Err(errors)
        }
    }

    /// Returns the LLVM basic type for `type_id`, if it has a runtime value.
    #[must_use]
    pub fn basic_type(&self, type_id: TypeId) -> Option<BasicTypeEnum<'ctx>> {
        self.basic_types.get(&type_id).copied()
    }

    /// Returns the LLVM void type for `type_id`, if it is `unit`.
    #[must_use]
    pub fn void_type(&self, type_id: TypeId) -> Option<VoidType<'ctx>> {
        self.void_types.get(&type_id).copied()
    }

    /// Returns the LLVM struct type for a nominal struct symbol.
    #[must_use]
    pub fn struct_type(&self, name: InternedStr) -> Option<StructType<'ctx>> {
        self.struct_types.get(&name).copied()
    }

    /// Returns the LLVM function type for a function signature `TypeId`.
    pub fn function_type(
        &self,
        context: &'ctx Context,
        arena: &TypeArena,
        type_id: TypeId,
    ) -> Result<LlvmFunctionType<'ctx>, CodegenError> {
        match arena.kind(type_id) {
            TypeKind::Function(signature) => self
                .function_type_for_signature(context, arena, signature)?
                .ok_or(CodegenError::MissingType {
                    ty: type_id,
                    span: None,
                }),
            _ => Err(CodegenError::MissingType {
                ty: type_id,
                span: None,
            }),
        }
    }

    /// Returns the LLVM function type for a parameter/return signature.
    pub fn direct_function_type(
        &self,
        return_type: TypeId,
        params: &[TypeId],
    ) -> Result<LlvmFunctionType<'ctx>, CodegenError> {
        let param_types = params
            .iter()
            .filter_map(|ty| self.basic_type(*ty))
            .map(BasicMetadataTypeEnum::from)
            .collect::<Vec<_>>();
        if param_types.len() != params.len() {
            let missing = params
                .iter()
                .find(|ty| self.basic_type(**ty).is_none())
                .copied()
                .unwrap_or(return_type);
            return Err(CodegenError::MissingType {
                ty: missing,
                span: None,
            });
        }

        if let Some(void_type) = self.void_type(return_type) {
            Ok(void_type.fn_type(&param_types, false))
        } else if let Some(basic) = self.basic_type(return_type) {
            Ok(basic.fn_type(&param_types, false))
        } else {
            Err(CodegenError::MissingType {
                ty: return_type,
                span: None,
            })
        }
    }

    fn function_type_for_signature(
        &self,
        _context: &'ctx Context,
        _arena: &TypeArena,
        signature: &thagore_typeck::FunctionType,
    ) -> Result<Option<LlvmFunctionType<'ctx>>, CodegenError> {
        let param_types = signature
            .params
            .iter()
            .filter_map(|ty| self.basic_type(*ty))
            .map(BasicMetadataTypeEnum::from)
            .collect::<Vec<_>>();
        if param_types.len() != signature.params.len() {
            let missing = signature
                .params
                .iter()
                .find(|ty| self.basic_type(**ty).is_none())
                .copied()
                .unwrap_or(signature.return_type);
            return Err(CodegenError::MissingType {
                ty: missing,
                span: None,
            });
        }

        if let Some(void_type) = self.void_type(signature.return_type) {
            Ok(Some(void_type.fn_type(&param_types, false)))
        } else if let Some(basic) = self.basic_type(signature.return_type) {
            Ok(Some(basic.fn_type(&param_types, false)))
        } else {
            Ok(None)
        }
    }

    /// Returns the pointer type used for stack slots.
    pub fn ptr_to(&self, ty: TypeId) -> Result<PointerType<'ctx>, CodegenError> {
        self.basic_type(ty)
            .map(|basic| basic.ptr_type(AddressSpace::default()))
            .ok_or(CodegenError::MissingType { ty, span: None })
    }
}
