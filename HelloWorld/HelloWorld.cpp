//=============================================================================
// FILE:
//    HelloWorld.cpp
//
// DESCRIPTION:
//    Visits all functions in a module, prints their names and the number of
//    arguments via stderr. Strictly speaking, this is an analysis pass (i.e.
//    the functions are not modified). However, in order to keep things simple
//    there's no 'print' method here (every analysis pass should implement it).
//
// USAGE:
//    New PM
//      opt -load-pass-plugin=libHelloWorld.dylib -passes="hello-world" `\`
//        -disable-output <input-llvm-file>
//
//
// License: MIT
//=============================================================================
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FormatVariadic.h"
#include <map>

using namespace llvm;

//-----------------------------------------------------------------------------
// HelloWorld implementation
//-----------------------------------------------------------------------------
// No need to expose the internals of the pass to the outside world - keep
// everything in an anonymous namespace.
namespace {

struct Expression {
  unsigned Opcode;
  unsigned LHS, RHS;

  Expression(unsigned Op, unsigned L, unsigned R) : Opcode(Op), LHS(L), RHS(R) {
    // Handle commutativity for addition and multiplication
    if (Op == Instruction::Add || Op == Instruction::Mul) {
      if (L > R) std::swap(LHS, RHS);
    }
  }

  bool operator<(const Expression &Other) const {
    return std::tie(Opcode, LHS, RHS) < std::tie(Other.Opcode, Other.LHS, Other.RHS);
  }
};

std::string getOpcodeName(unsigned Opcode) {
  switch (Opcode) {
  case Instruction::Add: return "add";
  case Instruction::Sub: return "sub";
  case Instruction::Mul: return "mul";
  case Instruction::UDiv:
  case Instruction::SDiv: return "div";
  default: return "unknown";
  }
}


// This method implements what the pass does
void visitor(Function &F) {
  errs() << "ValueNumbering: " << F.getName() << "\n";

  std::map<Expression, unsigned> LVNTable;
  std::map<Value *, unsigned> ValueNumbers;
  unsigned nextValueNumber = 1;

  for (auto &BB : F) {
    for (auto &I : BB) {
      //handle store operation
      if (auto *Store = dyn_cast<StoreInst>(&I)) {
        Value *StoredValue = Store->getValueOperand(); //value being stored
        Value *Pointer = Store->getPointerOperand(); //Destination where it's stored

        if (!ValueNumbers.count(StoredValue)) {
          ValueNumbers[StoredValue] = nextValueNumber++; //If StoredValue is new, assign a fresh number
        }
        ValueNumbers[Pointer] = ValueNumbers[StoredValue]; //Maps Pointer to the stored value’s number
        //errs() << I << "\t" << ValueNumbers[Pointer] << " = " << ValueNumbers[StoredValue] << "\n";
        errs() << formatv("{0,-40} {1} = {2}\n", I, ValueNumbers[Pointer], ValueNumbers[StoredValue]);
      } else if (auto *Load = dyn_cast<LoadInst>(&I)) { //handle load operation
        Value *Pointer = Load->getPointerOperand();

        if (ValueNumbers.count(Pointer)) {
          ValueNumbers[&I] = ValueNumbers[Pointer]; //If Pointer is known, assigns same number
        } else {
          ValueNumbers[&I] = nextValueNumber++; //Otherwise, assigns a fresh number
        }
        //errs() << I << "\t" << ValueNumbers[&I] << " = " << ValueNumbers[Pointer] << "\n";
        errs() << formatv("{0,-40} {1} = {2}\n", I, ValueNumbers[&I], ValueNumbers[Pointer]);
      } else if (auto *BinOp = dyn_cast<BinaryOperator>(&I)) {  //handle arithmetic operation
        unsigned LHS = ValueNumbers[BinOp->getOperand(0)]; //Get value numbers for operands
        unsigned RHS = ValueNumbers[BinOp->getOperand(1)];
        Expression Expr(BinOp->getOpcode(), LHS, RHS); //Creates a unique key to detect redundancy

        if (LVNTable.count(Expr)) { //If expression exists, reuse its value number (redundant label)
          ValueNumbers[&I] = LVNTable[Expr];
          //errs() << I << "\t" << ValueNumbers[&I] << " = "
                 //<< LHS << " " << getOpcodeName(BinOp->getOpcode()) << " " << RHS << " (redundant)\n";
          errs() << formatv("{0,-40} {1} = {2} {3} {4} (redundant)\n", I, ValueNumbers[&I], LHS, getOpcodeName(BinOp->getOpcode()), RHS);
        } else { //Otherwise, assigns a fresh number and stores it
          ValueNumbers[&I] = nextValueNumber++;
          LVNTable[Expr] = ValueNumbers[&I];
          //errs() << I << "\t" << ValueNumbers[&I] << " = "
                 //<< LHS << " " << getOpcodeName(BinOp->getOpcode()) << " " << RHS << "\n";
          errs() << formatv("{0,-40} {1} = {2} {3} {4}\n", I, ValueNumbers[&I], LHS, getOpcodeName(BinOp->getOpcode()), RHS);
        }
      }
    }
  }
}

// New PM implementation
struct HelloWorld : PassInfoMixin<HelloWorld> {
  // Main entry point, takes IR unit to run the pass on (&F) and the
  // corresponding pass manager (to be queried if need be)
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    visitor(F);
    return PreservedAnalyses::all();
  }

  // Without isRequired returning true, this pass will be skipped for functions
  // decorated with the optnone LLVM attribute. Note that clang -O0 decorates
  // all functions with optnone.
  static bool isRequired() { return true; }
};
} // namespace

//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getHelloWorldPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "HelloWorld", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "hello-world") {
                    FPM.addPass(HelloWorld());
                    return true;
                  }
                  return false;
                });
          }};
}

// This is the core interface for pass plugins. It guarantees that 'opt' will
// be able to recognize HelloWorld when added to the pass pipeline on the
// command line, i.e. via '-passes=hello-world'
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getHelloWorldPluginInfo();
}
