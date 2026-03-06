use std::fs;
use std::process::Command;

use thagore_ast::InternedStr;
use thagore_codegen::{Codegen, CodegenOptions, OptimizationLevel, OutputConfig};
use thagore_ir::{
    BasicBlock, BinOp, BlockId, Const, Instr, IrFunction, IrModule, IrStruct, Terminator, Value,
};
use thagore_typeck::TypeArena;

fn symbol(index: u32) -> InternedStr {
    InternedStr::new(index)
}

fn temp_file(name: &str, ext: &str) -> std::path::PathBuf {
    let mut path = std::env::temp_dir();
    path.push(format!(
        "thagore_codegen_{name}_{}_{}",
        std::process::id(),
        ext
    ));
    path.set_extension(ext);
    path
}

fn build_i32_function(name: InternedStr, return_value: i64, types: &TypeArena) -> IrFunction {
    let mut function = IrFunction::new(name, types.i32());
    function.entry = BlockId::new(0);
    function.value_types = vec![types.i32()];
    function.blocks.push(BasicBlock {
        id: BlockId::new(0),
        instructions: vec![Instr::Const(Value::new(0), Const::Int(return_value))],
        terminator: Some(Terminator::Return(Some(Value::new(0)))),
        successors: vec![],
        predecessors: vec![],
    });
    function
}

#[test]
fn emits_primitive_constants_and_valid_llvm_ir() {
    let types = TypeArena::new();
    let module_name = symbol(1);
    let function_name = symbol(2);
    let function = build_i32_function(function_name, 7, &types);

    let mut codegen = Codegen::new();
    codegen.register_symbol_name(module_name, "test_module");
    codegen.register_symbol_name(function_name, "main");
    let result = codegen
        .emit(
            &IrModule {
                name: module_name,
                functions: vec![function],
                structs: vec![],
            },
            &types,
        )
        .expect("codegen should succeed");
    assert!(result.llvm_ir.contains("define i32 @main()"));
    assert!(result.llvm_ir.contains("ret i32 7"));

    let ll_path = temp_file("primitive", "ll");
    fs::write(&ll_path, &result.llvm_ir).expect("write ll");
    let status = Command::new("llvm-as")
        .arg(&ll_path)
        .arg("-o")
        .arg(temp_file("primitive", "bc"))
        .status()
        .expect("llvm-as");
    assert!(status.success());
    let _ = fs::remove_file(ll_path);
}

#[test]
fn emits_function_calls_and_struct_field_access() {
    let mut types = TypeArena::new();
    let point_ty = types.reserve_struct(symbol(10));
    types.set_struct_fields(
        point_ty,
        vec![
            thagore_typeck::StructField {
                name: symbol(11),
                ty: types.i32(),
            },
            thagore_typeck::StructField {
                name: symbol(12),
                ty: types.i32(),
            },
        ],
    );

    let callee = build_i32_function(symbol(3), 2, &types);
    let mut caller = IrFunction::new(symbol(4), types.i32());
    caller.entry = BlockId::new(0);
    caller.value_types = vec![point_ty, types.i32(), types.i32(), types.i32(), types.i32()];
    caller.params = vec![(Value::new(0), point_ty)];
    caller.blocks.push(BasicBlock {
        id: BlockId::new(0),
        instructions: vec![
            Instr::GetField(Value::new(1), Value::new(0), symbol(11)),
            Instr::Call(Value::new(2), symbol(3), vec![]),
            Instr::BinOp(Value::new(3), BinOp::Add, Value::new(1), Value::new(2)),
        ],
        terminator: Some(Terminator::Return(Some(Value::new(3)))),
        successors: vec![],
        predecessors: vec![],
    });

    let mut codegen = Codegen::new();
    codegen.register_symbol_name(symbol(1), "cg_structs");
    codegen.register_symbol_name(symbol(3), "callee");
    codegen.register_symbol_name(symbol(4), "main");
    codegen.register_symbol_name(symbol(10), "Point");
    let result = codegen
        .emit(
            &IrModule {
                name: symbol(1),
                functions: vec![callee, caller],
                structs: vec![IrStruct {
                    name: symbol(10),
                    fields: vec![(symbol(11), types.i32()), (symbol(12), types.i32())],
                }],
            },
            &types,
        )
        .expect("codegen should succeed");
    assert!(result.llvm_ir.contains("%Point = type"));
    assert!(result.llvm_ir.contains("call i32 @callee()"));
}

#[test]
fn emits_control_flow_and_optimization_pipeline() {
    let types = TypeArena::new();
    let mut function = IrFunction::new(symbol(5), types.i32());
    function.entry = BlockId::new(0);
    function.value_types = vec![types.bool(), types.i32(), types.i32(), types.i32()];
    function.blocks = vec![
        BasicBlock {
            id: BlockId::new(0),
            instructions: vec![Instr::Const(Value::new(0), Const::Bool(true))],
            terminator: Some(Terminator::Branch(
                Value::new(0),
                BlockId::new(1),
                BlockId::new(2),
            )),
            successors: vec![BlockId::new(1), BlockId::new(2)],
            predecessors: vec![],
        },
        BasicBlock {
            id: BlockId::new(1),
            instructions: vec![Instr::Const(Value::new(1), Const::Int(1))],
            terminator: Some(Terminator::Jump(BlockId::new(3))),
            successors: vec![BlockId::new(3)],
            predecessors: vec![BlockId::new(0)],
        },
        BasicBlock {
            id: BlockId::new(2),
            instructions: vec![Instr::Const(Value::new(2), Const::Int(2))],
            terminator: Some(Terminator::Jump(BlockId::new(3))),
            successors: vec![BlockId::new(3)],
            predecessors: vec![BlockId::new(0)],
        },
        BasicBlock {
            id: BlockId::new(3),
            instructions: vec![Instr::Phi(
                Value::new(3),
                vec![
                    (Value::new(1), BlockId::new(1)),
                    (Value::new(2), BlockId::new(2)),
                ],
            )],
            terminator: Some(Terminator::Return(Some(Value::new(3)))),
            successors: vec![],
            predecessors: vec![BlockId::new(1), BlockId::new(2)],
        },
    ];

    let mut codegen = Codegen::new();
    codegen.register_symbol_name(symbol(100), "opt_module");
    codegen.register_symbol_name(symbol(5), "main");
    let mut options = CodegenOptions::default();
    options.optimization = OptimizationLevel::O2;
    codegen.set_options(options);
    let result = codegen
        .emit(
            &IrModule {
                name: symbol(100),
                functions: vec![function],
                structs: vec![],
            },
            &types,
        )
        .expect("optimized codegen should succeed");
    assert!(result.llvm_ir.contains("define i32 @main()"));
}

#[test]
fn emits_outputs_and_links_native_binary() {
    let types = TypeArena::new();
    let function = build_i32_function(symbol(6), 0, &types);
    let ll = temp_file("out", "ll");
    let bc = temp_file("out", "bc");
    let obj = temp_file("out", "o");
    let bin = temp_file("out", "bin");

    let mut codegen = Codegen::new();
    codegen.register_symbol_name(symbol(101), "out_module");
    codegen.register_symbol_name(symbol(6), "main");
    let mut options = CodegenOptions::default();
    options.output = OutputConfig {
        llvm_ir: Some(ll.clone()),
        bitcode: Some(bc.clone()),
        object: Some(obj.clone()),
        binary: Some(bin.clone()),
    };
    codegen.set_options(options);
    let result = codegen
        .emit(
            &IrModule {
                name: symbol(101),
                functions: vec![function],
                structs: vec![],
            },
            &types,
        )
        .expect("codegen output should succeed");
    assert!(result.artifacts.llvm_ir.is_some());
    assert!(result.artifacts.bitcode.is_some());
    assert!(result.artifacts.object.is_some());
    assert!(result.artifacts.binary.is_some());

    let status = Command::new(&bin).status().expect("run binary");
    assert_eq!(status.code(), Some(0));

    let _ = fs::remove_file(ll);
    let _ = fs::remove_file(bc);
    let _ = fs::remove_file(obj);
    let _ = fs::remove_file(bin);
}
