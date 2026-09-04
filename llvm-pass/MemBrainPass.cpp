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
#include "llvm/Support/ErrorHandling.h"
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

static std::atomic<uint64_t> g_cloneCounter{0};

// Clones a bottom-up call chain [F_0, F_1, ..., F_k] where F_i calls F_{i-1}.
// Returns the cloned chain [F_0', F_1', ..., F_k'] with all internal calls rewired down to the cloned leaf F_0'.
static SmallVector<Function*, 4> cloneCallChain(const SmallVectorImpl<Function*> &chain) {
    SmallVector<Function*, 4> clonedChain;
    Function *clonedDownstream = nullptr;

    for (size_t i = 0; i < chain.size(); ++i) {
        Function *origF = chain[i];
        ValueToValueMapTy VMap;
        uint64_t cloneId = ++g_cloneCounter;
        std::string cloneName = (origF->getName() + "_mbclone_" + Twine(cloneId)).str();

        Function *ClonedF = CloneFunction(origF, VMap);
        ClonedF->setName(cloneName);
        ClonedF->setSubprogram(nullptr);
        ClonedF->setLinkage(GlobalValue::InternalLinkage);

        SmallVector<BasicBlock*, 32> blocks;
        for (BasicBlock &BB : *ClonedF) blocks.push_back(&BB);
        remapInstructionsInBlocks(blocks, VMap);

        // Remap recursive calls within the clone to target ClonedF itself
        for (BasicBlock &BB : *ClonedF) {
            for (Instruction &I : BB) {
                if (auto *CB = dyn_cast<CallBase>(&I)) {
                    if (CB->getCalledOperand()->stripPointerCasts() == origF) {
                        CB->setCalledFunction(ClonedF);
                    }
                }
            }
        }

        // Rewire internal call to the cloned downstream callee
        if (i > 0 && clonedDownstream) {
            Function *origDownstream = chain[i - 1];
            for (BasicBlock &BB : *ClonedF) {
                for (Instruction &I : BB) {
                    if (auto *CB = dyn_cast<CallBase>(&I)) {
                        if (CB->getCalledOperand()->stripPointerCasts() == origDownstream) {
                            CB->setCalledFunction(clonedDownstream);
                        }
                    }
                }
            }
        }

        clonedDownstream = ClonedF;
        clonedChain.push_back(ClonedF);
    }

    return clonedChain;
}

struct CallChainWorkItem {
    SmallVector<Function*, 4> chain;
    uint32_t currentDepth{1};
};

// Allocation-driven, bottom-up function cloning (Section IV-A & Figure 3).
// Starting from the inner-most node (containing an allocation instruction),
// the pass walks each path back (towards main) and creates a separate copy
// (including the subtree) of the first node it finds with multiple parents.
// Termination occurs when there are no call paths of length n (or less) that end at the same allocation site.
static void performCallPathFunctionCloning(Module &M, uint32_t maxDepth) {
    if (maxDepth <= 1) return;

    // Step 1: Identify all original functions containing allocation instructions
    SmallPtrSet<Function*, 32> origAllocFuncs;
    for (Function &F : M) {
        if (F.isDeclaration() || F.getName().starts_with("membrain_")) continue;
        for (BasicBlock &BB : F) {
            for (Instruction &I : BB) {
                if (auto *CB = dyn_cast<CallBase>(&I)) {
                    if (isAllocationCall(CB)) {
                        origAllocFuncs.insert(&F);
                        break;
                    }
                }
            }
        }
    }

    // Step 2: Initialize worklist with path length 1 for each allocation-bearing function
    std::vector<CallChainWorkItem> worklist;
    for (Function *F : origAllocFuncs) {
        CallChainWorkItem item;
        item.chain.push_back(F);
        item.currentDepth = 1;
        worklist.push_back(std::move(item));
    }

    // Step 3: Walk paths bottom-up towards main up to maxDepth
    while (!worklist.empty()) {
        CallChainWorkItem item = std::move(worklist.back());
        worklist.pop_back();

        if (item.currentDepth >= maxDepth) {
            continue; // Path is already disambiguated up to length n
        }

        Function *currF = item.chain.back();

        // Group callers by parent Function node (CallGraph nodes are functions)
        std::map<Function*, SmallVector<CallBase*, 4>> callersByFunc;
        for (User *U : currF->users()) {
            if (auto *CB = dyn_cast<CallBase>(U)) {
                if (CB->getCalledOperand()->stripPointerCasts() == currF) {
                    Function *callerF = CB->getFunction();
                    if (callerF && !callerF->isDeclaration() &&
                        callerF != currF &&
                        !callerF->getName().starts_with("membrain_")) {
                        // Prevent cycles in the call chain
                        bool inChain = false;
                        for (Function *f : item.chain) {
                            if (f == callerF) { inChain = true; break; }
                        }
                        if (!inChain) {
                            callersByFunc[callerF].push_back(CB);
                        }
                    }
                }
            }
        }

        if (callersByFunc.empty()) {
            // Reached an uncalled root function
            continue;
        }

        if (callersByFunc.size() == 1) {
            // Exactly one parent function at this level: no branch point
            Function *singleParent = callersByFunc.begin()->first;
            if (singleParent->getName() == "main") {
                continue; // Reached main
            }
            item.chain.push_back(singleParent);
            item.currentDepth++;
            worklist.push_back(std::move(item));
            continue;
        }

        // Multiple parent functions found: this node has multiple callers!
        // First parent continues to use the existing chain
        auto it = callersByFunc.begin();
        Function *firstParent = it->first;
        if (firstParent->getName() != "main" && (item.currentDepth + 1 < maxDepth)) {
            CallChainWorkItem firstItem = item;
            firstItem.chain.push_back(firstParent);
            firstItem.currentDepth++;
            worklist.push_back(std::move(firstItem));
        }

        // Each additional parent gets a duplicate copy of the subtree (Section IV-A & Figure 3)
        ++it;
        for (; it != callersByFunc.end(); ++it) {
            Function *parentF = it->first;
            const auto &callSites = it->second;

            // Clone chain [F_0, ..., currF]
            SmallVector<Function*, 4> clonedChain = cloneCallChain(item.chain);

            Function *clonedRoot = clonedChain.back();

            // Rewire all calls in parentF to target clonedRoot
            for (CallBase *cb : callSites) {
                cb->setCalledFunction(clonedRoot);
            }

            // If not at maxDepth and not main, continue specializing the new cloned path
            if (parentF->getName() != "main" && (item.currentDepth + 1 < maxDepth)) {
                CallChainWorkItem newItem;
                newItem.chain = std::move(clonedChain);
                newItem.chain.push_back(parentF);
                newItem.currentDepth = item.currentDepth + 1;
                worklist.push_back(std::move(newItem));
            }
        }
    }
}

struct MemBrainPass : public PassInfoMixin<MemBrainPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
        // Step 1: Perform Call Path Function Cloning
        if (const char *envDepth = std::getenv("MEMBRAIN_CLONE_DEPTH")) {
            int cloneDepth = std::atoi(envDepth);
            if (cloneDepth > 0) {
                performCallPathFunctionCloning(M, cloneDepth);
            }
        }

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
            SmallString<256> outPath;
            if (const char *envDir = std::getenv("MEMBRAIN_SITES_DIR")) {
                outPath = envDir;
                sys::path::append(outPath, "allocation_sites_" + modStem + ".json");
            } else if (const char *envFile = std::getenv("MEMBRAIN_SITES_FILE")) {
                outPath = envFile;
            } else {
                report_fatal_error("[MemBrainPass] Error: Neither MEMBRAIN_SITES_DIR nor MEMBRAIN_SITES_FILE environment variable is set");
            }

            std::error_code EC;
            raw_fd_ostream os(outPath, EC, sys::fs::OF_None);
            if (EC) {
                report_fatal_error("[MemBrainPass] Error opening allocation sites output file '" +
                                   Twine(outPath) + "': " + EC.message());
            }
            os << formatv("{0:2}\n", json::Value(std::move(moduleSites)));
        }

        return modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "MemBrainPass", "v1.0",
        [](PassBuilder &PB) {
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
