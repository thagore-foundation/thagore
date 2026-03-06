//! LLVM optimization pipeline for generated Thagore modules.

use inkwell::module::Module;
use inkwell::passes::PassManager;
use inkwell::OptimizationLevel as LlvmOptimizationLevel;

/// Backend optimization level.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OptimizationLevel {
    /// Disable optimization.
    O0,
    /// Minimal optimization.
    O1,
    /// Default optimization.
    O2,
    /// Aggressive optimization.
    O3,
}

impl Default for OptimizationLevel {
    fn default() -> Self {
        Self::O0
    }
}

impl OptimizationLevel {
    /// Converts to the inkwell optimization level.
    #[must_use]
    pub const fn to_llvm(self) -> LlvmOptimizationLevel {
        match self {
            Self::O0 => LlvmOptimizationLevel::None,
            Self::O1 => LlvmOptimizationLevel::Less,
            Self::O2 => LlvmOptimizationLevel::Default,
            Self::O3 => LlvmOptimizationLevel::Aggressive,
        }
    }
}

/// Runs the LLVM optimization pipeline on the whole module.
pub fn optimize_module(module: &Module<'_>, level: OptimizationLevel) {
    let function_pm = PassManager::create(module);
    configure_function_pipeline(&function_pm, level);

    function_pm.initialize();
    for function in module.get_functions() {
        if function.count_basic_blocks() != 0 {
            function_pm.run_on(&function);
        }
    }
    function_pm.finalize();

    let module_pm = PassManager::create(());
    configure_module_pipeline(&module_pm, level);
    module_pm.run_on(module);
}

fn configure_function_pipeline(function_pm: &PassManager<inkwell::values::FunctionValue<'_>>, level: OptimizationLevel) {
    // Level 1: always-on scalar cleanups. These are deliberately enabled even
    // for `O0` because the frontend emits alloca/load/store heavy IR that must
    // be normalized before later backend stages can generate competitive code.
    function_pm.add_promote_memory_to_register_pass();
    function_pm.add_early_cse_pass();
    function_pm.add_instruction_combining_pass();
    function_pm.add_cfg_simplification_pass();

    if level == OptimizationLevel::O0 {
        return;
    }

    // Alias analyses improve LICM/GVN/DSE profitability on the flat IR we
    // lower today.
    function_pm.add_type_based_alias_analysis_pass();
    function_pm.add_scoped_no_alias_aa_pass();
    function_pm.add_basic_alias_analysis_pass();

    // Level 2: default optimized pipeline tuned for scalar + loop-heavy code.
    function_pm.add_sccp_pass();
    function_pm.add_gvn_pass();
    function_pm.add_dead_store_elimination_pass();
    function_pm.add_instruction_simplify_pass();
    function_pm.add_reassociate_pass();
    function_pm.add_loop_rotate_pass();
    function_pm.add_licm_pass();
    function_pm.add_ind_var_simplify_pass();
    function_pm.add_loop_idiom_pass();
    function_pm.add_loop_deletion_pass();
    function_pm.add_loop_vectorize_pass();
    function_pm.add_slp_vectorize_pass();
    function_pm.add_loop_unroll_pass();
    function_pm.add_loop_reroll_pass();
    function_pm.add_jump_threading_pass();
    function_pm.add_tail_call_elimination_pass();
    function_pm.add_memcpy_optimize_pass();
    function_pm.add_merged_load_store_motion_pass();
    function_pm.add_aggressive_inst_combiner_pass();
    function_pm.add_instruction_combining_pass();
    function_pm.add_cfg_simplification_pass();
    function_pm.add_aggressive_dce_pass();

    if level != OptimizationLevel::O3 {
        return;
    }

    // Level 3: more expensive loop and whole-function canonicalization.
    function_pm.add_loop_unroll_and_jam_pass();
    function_pm.add_loop_unswitch_pass();
    function_pm.add_scalar_repl_aggregates_pass_ssa();
    function_pm.add_aggressive_inst_combiner_pass();
}

fn configure_module_pipeline(module_pm: &PassManager<Module<'_>>, level: OptimizationLevel) {
    // Module cleanups that are safe and profitable regardless of optimization
    // level.
    module_pm.add_constant_merge_pass();
    module_pm.add_global_optimizer_pass();
    module_pm.add_ipsccp_pass();
    module_pm.add_dead_arg_elimination_pass();
    module_pm.add_function_attrs_pass();
    module_pm.add_global_dce_pass();
    module_pm.add_strip_dead_prototypes_pass();

    if level == OptimizationLevel::O0 {
        return;
    }

    module_pm.add_function_inlining_pass();
    module_pm.add_always_inliner_pass();
    module_pm.add_global_optimizer_pass();
    module_pm.add_constant_merge_pass();
    module_pm.add_function_attrs_pass();
    module_pm.add_global_dce_pass();

    if level != OptimizationLevel::O3 {
        return;
    }

    module_pm.add_argument_promotion_pass();
    module_pm.add_merge_functions_pass();
    module_pm.add_global_optimizer_pass();
}
