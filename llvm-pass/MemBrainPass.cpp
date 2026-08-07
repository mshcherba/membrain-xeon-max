// MemBrain LLVM Pass Plugin Skeleton
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

namespace {
struct MemBrainPass : public PassInfoMixin<MemBrainPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
        return PreservedAnalyses::all();
    }
};
} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "MemBrainPass", "v1.0",
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "membrain-pass") {
                        MPM.addPass(MemBrainPass());
                        return true;
                    }
                    return false;
                });
        }
    };
}
