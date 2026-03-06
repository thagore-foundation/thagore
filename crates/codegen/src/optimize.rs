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
    if level == OptimizationLevel::O0 {
        return;
    }

    let function_pm = PassManager::create(module);
    function_pm.add_promote_memory_to_register_pass();
    function_pm.add_instruction_combining_pass();
    function_pm.add_cfg_simplification_pass();
    function_pm.initialize();
    for function in module.get_functions() {
        function_pm.run_on(&function);
    }

    let module_pm = PassManager::create(());
    module_pm.add_function_inlining_pass();
    module_pm.add_cfg_simplification_pass();
    module_pm.run_on(module);
}
