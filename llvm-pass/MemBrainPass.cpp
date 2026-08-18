#include "llvm/ADT/Hashing.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cctype>

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

static bool isFreeCall(CallBase *CB) {
    Value *calledOp = CB->getCalledOperand()->stripPointerCasts();
    Function *F = dyn_cast<Function>(calledOp);
    if (!F) return false;

    StringRef name = F->getName();
    return name == "free" || name == "cfree" || name.starts_with("_Zdl") || name.starts_with("_Zda");
}

static bool leadsToAllocation(Function *F, SmallPtrSet<Function*, 16> &visited) {
    if (!F || F->isDeclaration() || F->isIntrinsic() || F->getName().starts_with("membrain_")) return false;
    if (!visited.insert(F).second) return false; // Cycle detection

    for (BasicBlock &BB : *F) {
        for (Instruction &I : BB) {
            if (auto *CB = dyn_cast<CallBase>(&I)) {
                if (isAllocationCall(CB)) return true;
                Value *calledOp = CB->getCalledOperand()->stripPointerCasts();
                if (Function *calledF = dyn_cast<Function>(calledOp)) {
                    if (leadsToAllocation(calledF, visited)) return true;
                }
            }
        }
    }
    return false;
}

static bool leadsToAllocation(Function *F) {
    SmallPtrSet<Function*, 16> visited;
    return leadsToAllocation(F, visited);
}

static std::atomic<uint64_t> g_cloneCounter{0};

static Function *cloneFunctionWithSubtree(Function *F,
                                         uint32_t depthRemaining,
                                         SmallPtrSet<Function*, 16> &visiting) {
    if (!F || F->isDeclaration() || F->isIntrinsic() || F->getName().starts_with("membrain_") || F->getName() == "main") {
        return F;
    }
    if (!visiting.insert(F).second) {
        return F; // Cycle detected: stop cloning down this recursive path
    }

    ValueToValueMapTy VMap;
    uint64_t cloneId = ++g_cloneCounter;
    std::string cloneName = (F->getName() + "_mbclone_" + Twine(cloneId)).str();

    Function *ClonedF = CloneFunction(F, VMap);
    ClonedF->setName(cloneName);
    ClonedF->setSubprogram(nullptr);

    // Recursively clone downstream callees that lead to allocation sites
    if (depthRemaining > 0) {
        for (BasicBlock &BB : *ClonedF) {
            for (Instruction &I : BB) {
                if (auto *CB = dyn_cast<CallBase>(&I)) {
                    if (isAllocationCall(CB)) continue;
                    Value *calledOp = CB->getCalledOperand()->stripPointerCasts();
                    if (Function *calleeF = dyn_cast<Function>(calledOp)) {
                        if (leadsToAllocation(calleeF)) {
                            Function *clonedCallee = cloneFunctionWithSubtree(calleeF, depthRemaining - 1, visiting);
                            if (clonedCallee && clonedCallee != calleeF) {
                                CB->setCalledFunction(clonedCallee);
                            }
                        }
                    }
                }
            }
        }
    }

    std::vector<BasicBlock*> blocks;
    for (BasicBlock &BB : *ClonedF) blocks.push_back(&BB);
    remapInstructionsInBlocks(blocks, VMap);

    visiting.erase(F);
    return ClonedF;
}

static void performCallPathFunctionCloning(Module &M, uint32_t maxDepth = 4) {
    for (uint32_t depth = 0; depth < maxDepth; ++depth) {
        std::vector<Function*> funcsToProcess;
        for (Function &F : M) {
            if (F.isDeclaration() || F.isIntrinsic() || F.getName().starts_with("membrain_") || F.getName() == "main") {
                continue;
            }
            if (leadsToAllocation(&F)) {
                funcsToProcess.push_back(&F);
            }
        }

        bool madeChanges = false;
        for (Function *F : funcsToProcess) {
            SmallVector<CallBase*, 8> callers;
            for (User *U : F->users()) {
                if (auto *CB = dyn_cast<CallBase>(U)) {
                    if (CB->getCalledOperand()->stripPointerCasts() == F) {
                        Function *callerFunc = CB->getFunction();
                        if (callerFunc && !callerFunc->getName().starts_with("membrain_")) {
                            callers.push_back(CB);
                        }
                    }
                }
            }

            // If function has multiple callers, clone function and its subtree for each additional caller
            if (callers.size() > 1) {
                for (size_t i = 1; i < callers.size(); ++i) {
                    SmallPtrSet<Function*, 16> visiting;
                    uint32_t remaining = (maxDepth > depth + 1) ? (maxDepth - (depth + 1)) : 0;
                    Function *ClonedF = cloneFunctionWithSubtree(F, remaining, visiting);
                    if (ClonedF && ClonedF != F) {
                        callers[i]->setCalledFunction(ClonedF);
                        madeChanges = true;
                    }
                }
            }
        }

        if (!madeChanges) {
            break; // Call graph reached steady state with unique call paths
        }
    }
}




struct MemBrainPass : public PassInfoMixin<MemBrainPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
        // Step 1: Perform Call Path Function Cloning (depth n=4)
        performCallPathFunctionCloning(M, 4);


        // Step 2: Annotate Allocation Sites and Rewrite IR
        LLVMContext &Ctx = M.getContext();
        bool modified = false;

        Type *sizeTy = Type::getInt64Ty(Ctx);
        Type *int32Ty = Type::getInt32Ty(Ctx);
        Type *ptrTy = PointerType::getUnqual(Ctx);
        FunctionType *hookTy = FunctionType::get(ptrTy, {sizeTy, int32Ty}, false);
        FunctionCallee membrainAllocCallee = M.getOrInsertFunction("membrain_alloc", hookTy);

        FunctionType *callocHookTy = FunctionType::get(ptrTy, {sizeTy, sizeTy, int32Ty}, false);
        FunctionCallee membrainCallocCallee = M.getOrInsertFunction("membrain_calloc", callocHookTy);

        FunctionType *reallocHookTy = FunctionType::get(ptrTy, {ptrTy, sizeTy, int32Ty}, false);
        FunctionCallee membrainReallocCallee = M.getOrInsertFunction("membrain_realloc", reallocHookTy);

        FunctionType *alignedAllocHookTy = FunctionType::get(ptrTy, {sizeTy, sizeTy, int32Ty}, false);
        FunctionCallee membrainAlignedAllocCallee = M.getOrInsertFunction("membrain_aligned_alloc", alignedAllocHookTy);

        FunctionType *posixMemalignHookTy = FunctionType::get(int32Ty, {ptrTy, sizeTy, sizeTy, int32Ty}, false);
        FunctionCallee membrainPosixMemalignCallee = M.getOrInsertFunction("membrain_posix_memalign", posixMemalignHookTy);

        FunctionType *freeHookTy = FunctionType::get(Type::getVoidTy(Ctx), {ptrTy}, false);
        FunctionCallee membrainFreeCallee = M.getOrInsertFunction("membrain_free", freeHookTy);

        StringRef modPath = M.getName();
        StringRef stem = sys::path::stem(modPath);
        std::string rawStem = (stem.empty() || stem == "-") ? "module" : stem.str();

        std::string modStem;
        for (char c : rawStem) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
                modStem += c;
            } else {
                modStem += '_';
            }
        }

        // Expanded module hash mask (0xFFFFF = 1,048,576 buckets) ensuring non-colliding site_ids
        uint32_t moduleHash = (static_cast<uint32_t>(hash_value(modStem)) & 0xFFFFF) * 1000;
        uint32_t siteId = moduleHash + 1;

        json::Array moduleSites;

        for (Function &F : M) {
            if (F.isDeclaration() || F.getName().starts_with("membrain_")) continue;

            for (BasicBlock &BB : F) {
                for (auto InstIt = BB.begin(); InstIt != BB.end(); ) {
                    Instruction &I = *InstIt++;
                    auto *CB = dyn_cast<CallBase>(&I);
                    if (!CB || isa<InvokeInst>(CB)) continue;


                    if (isFreeCall(CB)) {
                        IRBuilder<> Builder(CB);
                        Value *ptrVal = CB->getArgOperand(0);
                        if (ptrVal->getType() != ptrTy) {
                            ptrVal = Builder.CreateBitCast(ptrVal, ptrTy);
                        }
                        CallInst *newCall = Builder.CreateCall(membrainFreeCallee, {ptrVal});
                        newCall->setDebugLoc(CB->getDebugLoc());
                        CB->eraseFromParent();
                        modified = true;
                    } else if (isAllocationCall(CB)) {
                        IRBuilder<> Builder(CB);
                        Value *siteIdVal = ConstantInt::get(int32Ty, siteId);
                        CallInst *newCall = nullptr;

                        Value *calledOp = CB->getCalledOperand()->stripPointerCasts();
                        Function *CalledF = dyn_cast<Function>(calledOp);
                        StringRef funcName = CalledF ? CalledF->getName() : "";

                        if (funcName == "posix_memalign") {
                            Value *memptrVal = CB->getArgOperand(0);
                            Value *alignVal = CB->getArgOperand(1);
                            Value *sizeVal = CB->getArgOperand(2);

                            if (memptrVal->getType() != ptrTy) {
                                memptrVal = Builder.CreateBitCast(memptrVal, ptrTy);
                            }
                            if (alignVal->getType() != sizeTy) {
                                alignVal = Builder.CreateZExtOrTrunc(alignVal, sizeTy);
                            }
                            if (sizeVal->getType() != sizeTy) {
                                sizeVal = Builder.CreateZExtOrTrunc(sizeVal, sizeTy);
                            }

                            newCall = Builder.CreateCall(membrainPosixMemalignCallee, {memptrVal, alignVal, sizeVal, siteIdVal});
                            newCall->setDebugLoc(CB->getDebugLoc());

                            CB->replaceAllUsesWith(newCall);
                            CB->eraseFromParent();
                            modified = true;
                        } else if (funcName == "calloc") {
                            Value *numVal = Builder.CreateZExtOrTrunc(CB->getArgOperand(0), sizeTy);
                            Value *sizeVal = Builder.CreateZExtOrTrunc(CB->getArgOperand(1), sizeTy);

                            newCall = Builder.CreateCall(membrainCallocCallee, {numVal, sizeVal, siteIdVal});
                            newCall->setDebugLoc(CB->getDebugLoc());

                            Value *replacementVal = newCall;
                            if (newCall->getType() != CB->getType()) {
                                if (CB->getType()->isPointerTy()) {
                                    replacementVal = Builder.CreateBitCast(newCall, CB->getType());
                                } else if (CB->getType()->isIntegerTy()) {
                                    replacementVal = Builder.CreatePtrToInt(newCall, CB->getType());
                                }
                            }

                            CB->replaceAllUsesWith(replacementVal);
                            CB->eraseFromParent();
                            modified = true;
                        } else if (funcName == "realloc") {
                            Value *ptrVal = CB->getArgOperand(0);
                            if (ptrVal->getType() != ptrTy) {
                                ptrVal = Builder.CreateBitCast(ptrVal, ptrTy);
                            }
                            Value *sizeVal = Builder.CreateZExtOrTrunc(CB->getArgOperand(1), sizeTy);

                            newCall = Builder.CreateCall(membrainReallocCallee, {ptrVal, sizeVal, siteIdVal});
                            newCall->setDebugLoc(CB->getDebugLoc());

                            Value *replacementVal = newCall;
                            if (newCall->getType() != CB->getType()) {
                                if (CB->getType()->isPointerTy()) {
                                    replacementVal = Builder.CreateBitCast(newCall, CB->getType());
                                } else if (CB->getType()->isIntegerTy()) {
                                    replacementVal = Builder.CreatePtrToInt(newCall, CB->getType());
                                }
                            }

                            CB->replaceAllUsesWith(replacementVal);
                            CB->eraseFromParent();
                            modified = true;
                        } else if (funcName == "aligned_alloc") {
                            Value *alignVal = Builder.CreateZExtOrTrunc(CB->getArgOperand(0), sizeTy);
                            Value *sizeVal = Builder.CreateZExtOrTrunc(CB->getArgOperand(1), sizeTy);

                            newCall = Builder.CreateCall(membrainAlignedAllocCallee, {alignVal, sizeVal, siteIdVal});
                            newCall->setDebugLoc(CB->getDebugLoc());

                            Value *replacementVal = newCall;
                            if (newCall->getType() != CB->getType()) {
                                if (CB->getType()->isPointerTy()) {
                                    replacementVal = Builder.CreateBitCast(newCall, CB->getType());
                                } else if (CB->getType()->isIntegerTy()) {
                                    replacementVal = Builder.CreatePtrToInt(newCall, CB->getType());
                                }
                            }

                            CB->replaceAllUsesWith(replacementVal);
                            CB->eraseFromParent();
                            modified = true;
                        } else {
                            Value *rawSizeVal = getAllocationSize(CB, Builder, sizeTy);
                            Value *sizeVal = rawSizeVal;
                            if (sizeVal->getType() != sizeTy) {
                                sizeVal = Builder.CreateZExtOrTrunc(sizeVal, sizeTy);
                            }

                            newCall = Builder.CreateCall(membrainAllocCallee, {sizeVal, siteIdVal});
                            newCall->setDebugLoc(CB->getDebugLoc());

                            Value *replacementVal = newCall;
                            if (newCall->getType() != CB->getType()) {
                                if (CB->getType()->isPointerTy()) {
                                    replacementVal = Builder.CreateBitCast(newCall, CB->getType());
                                } else if (CB->getType()->isIntegerTy()) {
                                    replacementVal = Builder.CreatePtrToInt(newCall, CB->getType());
                                }
                            }

                            CB->replaceAllUsesWith(replacementVal);
                            CB->eraseFromParent();
                            modified = true;
                        }


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
                        moduleSites.push_back(std::move(siteObj));

                        siteId++;
                    }
                }
            }
        }

        if (modified) {
            SmallString<128> outPath;
            if (const char *envDir = std::getenv("MEMBRAIN_SITES_DIR")) {
                outPath = envDir;
                sys::path::append(outPath, "allocation_sites_" + modStem + ".json");
            } else {
                outPath = "allocation_sites_" + modStem + ".json";
            }

            std::error_code EC;
            raw_fd_ostream os(outPath, EC, sys::fs::OF_None);
            if (EC) {
                errs() << "[MemBrainPass] Error writing allocation sites to '"
                       << outPath << "': " << EC.message() << "\n";
            } else {
                os << formatv("{0:2}\n", json::Value(std::move(moduleSites)));
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
