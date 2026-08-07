// MemBrain LLVM Pass Plugin - Site ID Tagging & IR Rewriting
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

static bool isAllocationCall(CallBase *CB) {
    Value *calledOp = CB->getCalledOperand()->stripPointerCasts();
    Function *F = dyn_cast<Function>(calledOp);
    if (!F) return false;

    StringRef name = F->getName();
    return name == "malloc" || name == "calloc" || name == "realloc" ||
           name == "posix_memalign" || name == "aligned_alloc" ||
           name.starts_with("_Znw") || name.starts_with("_Zna");
}

static Value *getAllocationSize(CallBase *CB, IRBuilder<> &Builder, Type *sizeTy) {
    Value *calledOp = CB->getCalledOperand()->stripPointerCasts();
    Function *F = dyn_cast<Function>(calledOp);
    if (!F) return CB->getArgOperand(0);

    StringRef name = F->getName();
    if (name == "calloc") {
        Value *num = Builder.CreateZExtOrTrunc(CB->getArgOperand(0), sizeTy);
        Value *elemSize = Builder.CreateZExtOrTrunc(CB->getArgOperand(1), sizeTy);
        return Builder.CreateMul(num, elemSize);
    }
    if (name == "realloc" || name == "aligned_alloc") {
        return CB->getArgOperand(1);
    }
    if (name == "posix_memalign") {
        return CB->getArgOperand(2);
    }
    return CB->getArgOperand(0);
}

struct MemBrainPass : public PassInfoMixin<MemBrainPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
        LLVMContext &Ctx = M.getContext();
        uint32_t siteId = 1;
        bool modified = false;

        // Function prototype for membrain_alloc(size_t size, uint32_t site_id)
        Type *sizeTy = Type::getInt64Ty(Ctx);
        Type *int32Ty = Type::getInt32Ty(Ctx);
        Type *ptrTy = PointerType::getUnqual(Ctx);
        FunctionType *hookTy = FunctionType::get(ptrTy, {sizeTy, int32Ty}, false);
        FunctionCallee membrainAllocCallee = M.getOrInsertFunction("membrain_alloc", hookTy);

        json::Array siteArray;

        for (Function &F : M) {
            if (F.isDeclaration() || F.getName().starts_with("membrain_")) continue;

            for (BasicBlock &BB : F) {
                for (auto InstIt = BB.begin(); InstIt != BB.end(); ) {
                    Instruction &I = *InstIt++;
                    auto *CB = dyn_cast<CallBase>(&I);
                    if (!CB) continue;

                    if (isAllocationCall(CB)) {
                        IRBuilder<> Builder(CB);
                        Value *rawSizeVal = getAllocationSize(CB, Builder, sizeTy);
                        Value *sizeVal = rawSizeVal;
                        if (sizeVal->getType() != sizeTy) {
                            sizeVal = Builder.CreateZExtOrTrunc(sizeVal, sizeTy);
                        }

                        Value *siteIdVal = ConstantInt::get(int32Ty, siteId);
                        CallInst *newCall = Builder.CreateCall(membrainAllocCallee, {sizeVal, siteIdVal});
                        newCall->setDebugLoc(CB->getDebugLoc());

                        // Replace uses of original allocation with newCall
                        CB->replaceAllUsesWith(newCall);
                        CB->eraseFromParent();
                        modified = true;

                        // Metadata recording
                        json::Object siteObj;
                        siteObj["site_id"] = siteId;
                        siteObj["function"] = F.getName().str();

                        if (DILocation *Loc = newCall->getDebugLoc()) {
                            siteObj["file"] = Loc->getFilename().str();
                            siteObj["line"] = Loc->getLine();
                        } else {
                            siteObj["file"] = "unknown";
                            siteObj["line"] = 0;
                        }
                        siteArray.push_back(std::move(siteObj));

                        siteId++;
                    }
                }
            }
        }

        if (modified) {
            std::error_code EC;
            raw_fd_ostream os("allocation_sites.json", EC, sys::fs::OF_Append);
            if (!EC) {
                os << formatv("{0:2}", json::Value(std::move(siteArray)));
            }
        }


        return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
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
