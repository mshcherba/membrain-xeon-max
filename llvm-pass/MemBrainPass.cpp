// MemBrain LLVM Pass Plugin - Allocation Scanner Skeleton
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

static bool isAllocationFunction(StringRef name) {
    return name == "malloc" || name == "calloc" || name == "realloc" ||
           name == "posix_memalign" || name == "aligned_alloc" ||
           name == "_Znwm" || name == "_Znam" || name == "f90_alloc";
}

struct MemBrainPass : public PassInfoMixin<MemBrainPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
        uint32_t foundCount = 0;

        for (Function &F : M) {
            if (F.isDeclaration()) continue;

            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    auto *CB = dyn_cast<CallBase>(&I);
                    if (!CB) continue;

                    Function *calledFunc = CB->getCalledFunction();
                    if (!calledFunc) continue;

                    StringRef funcName = calledFunc->getName();
                    if (isAllocationFunction(funcName)) {
                        foundCount++;
                        errs() << "[MemBrainPass] Identified allocation call to '"
                               << funcName << "' in function '" << F.getName() << "'\n";
                    }
                }
            }
        }

        errs() << "[MemBrainPass] Total allocation sites detected: " << foundCount << "\n";
        return PreservedAnalyses::all();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "MemBrainPass", "v1.0",
        [](PassBuilder &PB) {
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel Level) {
                    MPM.addPass(MemBrainPass());
                });
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel Level) {
                    MPM.addPass(MemBrainPass());
                });
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

