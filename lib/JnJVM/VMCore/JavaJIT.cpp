//===----------- JavaJIT.cpp - Java just in time compiler -----------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#define DEBUG 0
#define JNJVM_COMPILE 0
#define JNJVM_EXECUTE 0

#include <string.h>

#include <llvm/Type.h>
#include <llvm/Support/CFG.h>
#include <llvm/Module.h>
#include <llvm/Constants.h>
#include <llvm/Type.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Function.h>
#include <llvm/Instructions.h>
#include <llvm/ModuleProvider.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/PassManager.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/CodeGen/MachineCodeEmitter.h>
#include <llvm/CodeGen/MachineBasicBlock.h>
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h"
#include "llvm/PassManager.h"
#include "llvm/ValueSymbolTable.h"
#include "llvm/Analysis/LoadValueNumbering.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Assembly/Writer.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Target/SubtargetFeature.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetMachineRegistry.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Streams.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"

#include <llvm/Transforms/IPO.h>


#include "mvm/JIT.h"
#include "mvm/Method.h"

#include "debug.h"
#include "JavaArray.h"
#include "JavaCache.h"
#include "JavaClass.h"
#include "JavaConstantPool.h"
#include "JavaObject.h"
#include "JavaJIT.h"
#include "JavaString.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "JnjvmModuleProvider.h"
#include "NativeUtil.h"
#include "Reader.h"
#include "Zip.h"

#include <iostream>


using namespace jnjvm;
using namespace llvm;

void Exception::print(mvm::PrintBuffer* buf) const {
  buf->write("Exception<>");
}

void JavaJIT::print(mvm::PrintBuffer* buf) const {
  buf->write("JavaJIT<>");
}

BasicBlock* JavaJIT::createBasicBlock(const char* name) {
  return new BasicBlock(name, llvmFunction);
}

Value* JavaJIT::top() {
  return stack.back().first;
}

Value* JavaJIT::pop() {
  llvm::Value * ret = top();
  stack.pop_back();
  return ret;
}

Value* JavaJIT::popAsInt() {
  llvm::Value * ret = top();
  const AssessorDesc* ass = topFunc();
  stack.pop_back();

  if (ret->getType() != Type::Int32Ty) {
    if (ass == AssessorDesc::dChar) {
      ret = new ZExtInst(ret, Type::Int32Ty, "", currentBlock);
    } else {
      ret = new SExtInst(ret, Type::Int32Ty, "", currentBlock);
    }
  }

  return ret;
}

void JavaJIT::push(llvm::Value* val, const AssessorDesc* ass) {
  assert(ass->llvmType == val->getType());
  stack.push_back(std::make_pair(val, ass));
}

void JavaJIT::push(std::pair<llvm::Value*, const AssessorDesc*> pair) {
  assert(pair.second->llvmType == pair.first->getType());
  stack.push_back(pair);
}


const AssessorDesc* JavaJIT::topFunc() {
  return stack.back().second;
}

uint32 JavaJIT::stackSize() {
  return stack.size();
}

std::pair<llvm::Value*, const AssessorDesc*> JavaJIT::popPair() {
  std::pair<Value*, const AssessorDesc*> ret = stack.back();
  stack.pop_back();
  return ret;
}

llvm::Function* JavaJIT::nativeCompile(void* natPtr) {
  
  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "native compile %s\n",
              compilingMethod->printString());
  
  bool stat = isStatic(compilingMethod->access);

  const FunctionType *funcType = compilingMethod->llvmType;
  
  bool jnjvm = false;
  natPtr = natPtr ? natPtr :
                    NativeUtil::nativeLookup(compilingClass, compilingMethod, jnjvm);
  
  
  
  compilingClass->isolate->protectModule->lock();
  Function* func = llvmFunction = new llvm::Function(funcType, 
                                                     GlobalValue::ExternalLinkage,
                                                     compilingMethod->printString(),
                                                     compilingClass->isolate->module);
  compilingClass->isolate->protectModule->unlock();
  
  if (jnjvm) {
    mvm::jit::executionEngine->addGlobalMapping(func, natPtr);
    return llvmFunction;
  }

  currentBlock = createBasicBlock("start");
  BasicBlock* executeBlock = createBasicBlock("execute");
  endBlock = createBasicBlock("end block");
  if (funcType->getReturnType() != Type::VoidTy)
    endNode = new PHINode(funcType->getReturnType(), "", endBlock);
  
  Value* buf = new CallInst(getSJLJBufferLLVM, "", currentBlock);
  Value* test = new CallInst(mvm::jit::setjmpLLVM, buf, "", currentBlock);
  test = new ICmpInst(ICmpInst::ICMP_EQ, test, mvm::jit::constantZero, "", currentBlock);
  new BranchInst(executeBlock, endBlock, test, currentBlock);
  if (compilingMethod->signature->ret->funcs != AssessorDesc::dVoid)
    endNode->addIncoming(compilingMethod->signature->ret->funcs->llvmNullConstant, currentBlock);
  
  currentBlock = executeBlock;
  if (isSynchro(compilingMethod->access))
    beginSynchronize();
  
  
  uint32 nargs = func->arg_size() + 1 + (stat ? 1 : 0); // + vm + cl/obj
  std::vector<Value*> nativeArgs;
  
  mvm::jit::protectConstants();//->lock();
  nativeArgs.push_back(
    ConstantExpr::getIntToPtr(ConstantInt::get(Type::Int64Ty,
                                               (int64_t)&(compilingClass->isolate->jniEnv)),
                    mvm::jit::ptrType));
  mvm::jit::unprotectConstants();//->unlock();

  uint32 index = 0;
  if (stat) {
#ifdef SINGLE_VM
    nativeArgs.push_back(new LoadInst(compilingClass->llvmDelegatee(), "", currentBlock));
#else
    Value* ld = new LoadInst(compilingClass->llvmVar(compilingClass->isolate->module), "", currentBlock);
    nativeArgs.push_back(new CallInst(getClassDelegateeLLVM, ld, "", currentBlock));
#endif
    index = 2;
  } else {
    index = 1;
  }
  for (Function::arg_iterator i = func->arg_begin(); 
       index < nargs; ++i, ++index) {
     
    nativeArgs.push_back(i);
  }
  
  
  const llvm::Type* valPtrType = compilingMethod->signature->nativeTypePtr;
  mvm::jit::protectConstants();//->lock();
  Value* valPtr = 
    ConstantExpr::getIntToPtr(ConstantInt::get(Type::Int64Ty, (uint64)natPtr),
                              valPtrType);
  mvm::jit::unprotectConstants();//->unlock();

  Value* result = new CallInst(valPtr, nativeArgs.begin(), nativeArgs.end(), "", currentBlock);
  if (funcType->getReturnType() != Type::VoidTy)
    endNode->addIncoming(result, currentBlock);
  new BranchInst(endBlock, currentBlock);

  currentBlock = endBlock; 
  if (isSynchro(compilingMethod->access))
    endSynchronize();
  
  new CallInst(jniProceedPendingExceptionLLVM, "", currentBlock);
  
  if (funcType->getReturnType() != Type::VoidTy)
    new ReturnInst(result, currentBlock);
  else
    new ReturnInst(currentBlock);
  
  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "end native compile %s\n",
              compilingMethod->printString());
  
  return llvmFunction;
}

#if defined __PPC__
extern "C"
{
# include <dis-asm.h>
# include <bfd.h>
};



static struct disassemble_info  info;
static int      initialised= 0;  

// this is the only function exported from this file

extern "C" int disassemble(unsigned int *addr)
{
  
  if (!initialised)
    {   
      INIT_DISASSEMBLE_INFO(info, stdout, fprintf);
      info.flavour=   bfd_target_elf_flavour;
      info.arch=    bfd_arch_powerpc;
      info.mach=    bfd_mach_ppc_750; // generic(ish) == PPC G3
      info.endian=    BFD_ENDIAN_BIG;
      info.buffer_length= 65536;
    }   
  info.buffer=     (bfd_byte *)addr;
  info.buffer_vma= (bfd_vma)(long)addr;
  return print_insn_big_powerpc((bfd_vma)(long)addr, &info);
  
}

#else
extern "C"
{
# include <bfd.h>	// bfd types
# include <dis-asm.h>	// disassemble_info
  int print_insn_i386_att(bfd_vma, disassemble_info *);
};


static struct disassemble_info	info;
static int			initialised= 0;

// this is the only function exported from this file

extern "C" int disassemble(unsigned int *addr)
{
  if (!initialised)
    {
      INIT_DISASSEMBLE_INFO(info, stdout, fprintf);
      info.flavour=	  bfd_target_elf_flavour;
      info.arch=	  bfd_arch_i386;
      info.mach=	  bfd_mach_i386_i386;
      info.endian=	  BFD_ENDIAN_LITTLE;
      info.buffer_length= 65536;
    }
  info.buffer=	   (bfd_byte *)addr;
  info.buffer_vma= (bfd_vma)(long)addr;
  return print_insn_i386_att((bfd_vma)(long)addr, &info);
}

#endif


void JavaJIT::beginSynchronize() {
  std::vector<Value*> argsSync;
  if (isVirtual(compilingMethod->access)) {
    argsSync.push_back(llvmFunction->arg_begin());
  } else {
    Value* arg = new LoadInst(compilingClass->staticVar(compilingClass->isolate->module), "", currentBlock);
#ifndef SINGLE_VM
    if (compilingClass->isolate == Jnjvm::bootstrapVM) {
      arg = new CallInst(getStaticInstanceLLVM, arg, "", currentBlock);
    }
#endif
    argsSync.push_back(arg);
  }
  new CallInst(aquireObjectLLVM, argsSync.begin(), argsSync.end(), "", currentBlock);
}

void JavaJIT::endSynchronize() {
  std::vector<Value*> argsSync;
  if (isVirtual(compilingMethod->access)) {
    argsSync.push_back(llvmFunction->arg_begin());
  } else {
    Value* arg = new LoadInst(compilingClass->staticVar(compilingClass->isolate->module), "", currentBlock);
#ifndef SINGLE_VM
    if (compilingClass->isolate == Jnjvm::bootstrapVM) {
      arg = new CallInst(getStaticInstanceLLVM, arg, "", currentBlock);
    }
#endif
    argsSync.push_back(arg);
  }
  new CallInst(releaseObjectLLVM, argsSync.begin(), argsSync.end(), "", currentBlock);    
}


Instruction* JavaJIT::inlineCompile(Function* parentFunction, BasicBlock*& curBB,
                                    BasicBlock* endExBlock,
                                    std::vector<Value*>& args) {
  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "inline compile %s\n",
              compilingMethod->printString());


  Attribut* codeAtt = Attribut::lookup(&compilingMethod->attributs,
                                       Attribut::codeAttribut);
  
  if (!codeAtt)
    JavaThread::get()->isolate->unknownError("unable to find the code attribut in %s",
                                             compilingMethod->printString());

  Reader* reader = codeAtt->toReader(compilingClass->bytes, codeAtt);
  maxStack = reader->readU2();
  maxLocals = reader->readU2();
  codeLen = reader->readU4();
  uint32 start = reader->cursor;
  
  reader->seek(codeLen, Reader::SeekCur);
  
  const FunctionType *funcType = compilingMethod->llvmType;
  returnType = funcType->getReturnType();

  llvmFunction = parentFunction;
  currentBlock = curBB;
  endExceptionBlock = 0;
  
  opcodeInfos = (Opinfo*)alloca(codeLen * sizeof(Opinfo));
  memset(opcodeInfos, 0, codeLen * sizeof(Opinfo));
  for (uint32 i = 0; i < codeLen; ++i) {
    opcodeInfos[i].exceptionBlock = endExBlock;
  }
  
  for (int i = 0; i < maxLocals; i++) {
    intLocals.push_back(new AllocaInst(Type::Int32Ty, "", currentBlock));
    doubleLocals.push_back(new AllocaInst(Type::DoubleTy, "", currentBlock));
    longLocals.push_back(new AllocaInst(Type::Int64Ty, "", currentBlock));
    floatLocals.push_back(new AllocaInst(Type::FloatTy, "", currentBlock));
    objectLocals.push_back(new AllocaInst(JavaObject::llvmType, "",
                                          currentBlock));
  }
  
  uint32 index = 0;
  uint32 count = 0;
  for (std::vector<Value*>::iterator i = args.begin();
       count < args.size(); ++i, ++index, ++count) {
    
    const Type* cur = (*i)->getType();

    if (cur == Type::Int64Ty ){
      new StoreInst(*i, longLocals[index], false, currentBlock);
      ++index;
    } else if (cur == Type::Int8Ty || cur == Type::Int16Ty) {
      new StoreInst(new ZExtInst(*i, Type::Int32Ty, "", currentBlock),
                    intLocals[index], false, currentBlock);
    } else if (cur == Type::Int32Ty) {
      new StoreInst(*i, intLocals[index], false, currentBlock);
    } else if (cur == Type::DoubleTy) {
      new StoreInst(*i, doubleLocals[index], false, currentBlock);
      ++index;
    } else if (cur == Type::FloatTy) {
      new StoreInst(*i, floatLocals[index], false, currentBlock);
    } else {
      new StoreInst(*i, objectLocals[index], false, currentBlock);
    }
  }
  
  exploreOpcodes(&compilingClass->bytes->elements[start], codeLen);
  
  endBlock = createBasicBlock("end");

  if (returnType != Type::VoidTy) {
    endNode = new PHINode(returnType, "", endBlock);
  }

  compileOpcodes(&compilingClass->bytes->elements[start], codeLen);
  
  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "--> end inline compiling %s\n",
              compilingMethod->printString());
  
  curBB = endBlock;
  return endNode;
    
}


llvm::Function* JavaJIT::javaCompile() {
  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "compiling %s\n",
              compilingMethod->printString());


  Attribut* codeAtt = Attribut::lookup(&compilingMethod->attributs,
                                       Attribut::codeAttribut);
  
  if (!codeAtt)
    JavaThread::get()->isolate->unknownError("unable to find the code attribut in %s",
                                             compilingMethod->printString());

  Reader* reader = codeAtt->toReader(compilingClass->bytes, codeAtt);
  maxStack = reader->readU2();
  maxLocals = reader->readU2();
  codeLen = reader->readU4();
  uint32 start = reader->cursor;
  
  reader->seek(codeLen, Reader::SeekCur);

  const FunctionType *funcType = compilingMethod->llvmType;
  returnType = funcType->getReturnType();
  
  compilingClass->isolate->protectModule->lock();
  Function* func = llvmFunction = new llvm::Function(funcType, 
                                                     GlobalValue::ExternalLinkage,
                                                     compilingMethod->printString(),
                                                     compilingClass->isolate->module);
  compilingClass->isolate->protectModule->unlock();
  
  currentBlock = createBasicBlock("start");
  endExceptionBlock = createBasicBlock("endExceptionBlock");
  unifiedUnreachable = createBasicBlock("unifiedUnreachable"); 

  opcodeInfos = (Opinfo*)alloca(codeLen * sizeof(Opinfo));
  memset(opcodeInfos, 0, codeLen * sizeof(Opinfo));
  for (uint32 i = 0; i < codeLen; ++i) {
    opcodeInfos[i].exceptionBlock = endExceptionBlock;
  }
    
#if JNJVM_EXECUTE > 0
    {
    std::vector<llvm::Value*> args;
    mvm::jit::protectConstants();//->lock();
    args.push_back(ConstantInt::get(Type::Int32Ty, (int64_t)compilingMethod));
    mvm::jit::unprotectConstants();//->unlock();
    new CallInst(printMethodStartLLVM, args.begin(), args.end(), "", currentBlock);
    }
#endif

  unsigned nbe = readExceptionTable(reader);
  
  for (int i = 0; i < maxLocals; i++) {
    intLocals.push_back(new AllocaInst(Type::Int32Ty, "", currentBlock));
    doubleLocals.push_back(new AllocaInst(Type::DoubleTy, "", currentBlock));
    longLocals.push_back(new AllocaInst(Type::Int64Ty, "", currentBlock));
    floatLocals.push_back(new AllocaInst(Type::FloatTy, "", currentBlock));
    objectLocals.push_back(new AllocaInst(JavaObject::llvmType, "",
                                          currentBlock));
  }
  
  uint32 index = 0;
  uint32 count = 0;
  for (Function::arg_iterator i = func->arg_begin(); 
       count < func->arg_size(); ++i, ++index, ++count) {
    
    const Type* cur = i->getType();

    if (cur == Type::Int64Ty ){
      new StoreInst(i, longLocals[index], false, currentBlock);
      ++index;
    } else if (cur == Type::Int8Ty || cur == Type::Int16Ty) {
      new StoreInst(new ZExtInst(i, Type::Int32Ty, "", currentBlock),
                    intLocals[index], false, currentBlock);
    } else if (cur == Type::Int32Ty) {
      new StoreInst(i, intLocals[index], false, currentBlock);
    } else if (cur == Type::DoubleTy) {
      new StoreInst(i, doubleLocals[index], false, currentBlock);
      ++index;
    } else if (cur == Type::FloatTy) {
      new StoreInst(i, floatLocals[index], false, currentBlock);
    } else {
      new StoreInst(i, objectLocals[index], false, currentBlock);
    }
  }
  
  
  exploreOpcodes(&compilingClass->bytes->elements[start], codeLen);
  

 
  endBlock = createBasicBlock("end");

  if (returnType != Type::VoidTy) {
    endNode = new PHINode(returnType, "", endBlock);
  }
  
  if (isSynchro(compilingMethod->access))
    beginSynchronize();

  compileOpcodes(&compilingClass->bytes->elements[start], codeLen); 
  currentBlock = endBlock;

  if (isSynchro(compilingMethod->access))
    endSynchronize();

#if JNJVM_EXECUTE > 0
    {
    std::vector<llvm::Value*> args;
    mvm::jit::protectConstants();//->lock();
    args.push_back(ConstantInt::get(Type::Int32Ty, (int64_t)compilingMethod));
    mvm::jit::unprotectConstants();//->unlock();
    new CallInst(printMethodEndLLVM, args.begin(), args.end(), "", currentBlock);
    }
#endif

  if (returnType != Type::VoidTy)
    new ReturnInst(endNode, endBlock);
  else
    new ReturnInst(endBlock);

  pred_iterator PI = pred_begin(endExceptionBlock);
  pred_iterator PE = pred_end(endExceptionBlock);
  if (PI == PE) {
    endExceptionBlock->eraseFromParent();
  } else {
    CallInst* ptr_eh_ptr = new CallInst(getExceptionLLVM, "eh_ptr", 
                                        endExceptionBlock);
    new CallInst(mvm::jit::unwindResume, ptr_eh_ptr, "", endExceptionBlock);
    new UnreachableInst(endExceptionBlock);
  }
  
  PI = pred_begin(unifiedUnreachable);
  PE = pred_end(unifiedUnreachable);
  if (PI == PE) {
    unifiedUnreachable->eraseFromParent();
  } else {
    new UnreachableInst(unifiedUnreachable);
  }
  
  mvm::jit::runPasses(llvmFunction, JavaThread::get()->perFunctionPasses);
  
  /*
  if (compilingMethod->name == compilingClass->isolate->asciizConstructUTF8("main")) {
    llvmFunction->print(llvm::cout);
    void* res = mvm::jit::executionEngine->getPointerToGlobal(llvmFunction);
    void* base = res;
    while (base <  (void*)((char*)res + ((mvm::Code*)res)->objectSize())) {
      printf("%08x\t", (unsigned)base);
      int n= disassemble((unsigned int *)base);
      printf("\n");
      base= ((void *)((char *)base + n));
    }
    printf("\n");
    fflush(stdout);
  }*/
  

  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "--> end compiling %s\n",
              compilingMethod->printString());
  
  if (nbe == 0 && codeLen < 50)
    compilingMethod->canBeInlined = false;

  return llvmFunction;
}


unsigned JavaJIT::readExceptionTable(Reader* reader) {
  uint16 nbe = reader->readU2();
  unsigned sync = isSynchro(compilingMethod->access) ? 1 : 0;
  nbe += sync;
  JavaCtpInfo* ctpInfo = compilingClass->ctpInfo;
  if (nbe) {
    supplLocal = new AllocaInst(JavaObject::llvmType, "exceptionVar",
                                currentBlock);
  }
  
  BasicBlock* realEndExceptionBlock = endExceptionBlock;
  if (sync) {
    BasicBlock* synchronizeExceptionBlock = createBasicBlock("synchronizeExceptionBlock");
    BasicBlock* trySynchronizeExceptionBlock = createBasicBlock("trySynchronizeExceptionBlock");
    realEndExceptionBlock = synchronizeExceptionBlock;
    std::vector<Value*> argsSync;
    if (isVirtual(compilingMethod->access)) {
      argsSync.push_back(llvmFunction->arg_begin());
    } else {
      Value* arg = new LoadInst(compilingClass->staticVar(compilingClass->isolate->module), "", currentBlock);
#ifndef SINGLE_VM
      if (compilingClass->isolate == Jnjvm::bootstrapVM) {
        arg = new CallInst(getStaticInstanceLLVM, arg, "", currentBlock);
      }
#endif
      argsSync.push_back(arg);
    }
    new CallInst(releaseObjectLLVM, argsSync.begin(), argsSync.end(), "", synchronizeExceptionBlock);
    new BranchInst(endExceptionBlock, synchronizeExceptionBlock);
    
    const PointerType* PointerTy_0 = mvm::jit::ptrType;
    std::vector<Value*> int32_eh_select_params;
    Instruction* ptr_eh_ptr = new CallInst(mvm::jit::llvmGetException, "eh_ptr", trySynchronizeExceptionBlock);
    int32_eh_select_params.push_back(ptr_eh_ptr);
    int32_eh_select_params.push_back(ConstantExpr::getCast(Instruction::BitCast, mvm::jit::personality, PointerTy_0));
    int32_eh_select_params.push_back(mvm::jit::constantPtrNull);
    new CallInst(mvm::jit::exceptionSelector, int32_eh_select_params.begin(), int32_eh_select_params.end(), "eh_select", trySynchronizeExceptionBlock);
    new BranchInst(synchronizeExceptionBlock, trySynchronizeExceptionBlock);

    for (uint16 i = 0; i < codeLen; ++i) {
      if (opcodeInfos[i].exceptionBlock == endExceptionBlock) {
        opcodeInfos[i].exceptionBlock = trySynchronizeExceptionBlock;
      }
    }
  }

  for (uint16 i = 0; i < nbe - sync; ++i) {
    Exception* ex = gc_new(Exception)();
    ex->startpc   = reader->readU2();
    ex->endpc     = reader->readU2();
    ex->handlerpc = reader->readU2();

    uint16 catche = reader->readU2();

    if (catche) {
      ex->catchClass = (Class*)ctpInfo->loadClass(catche);
    } else {
      ex->catchClass = Classpath::newThrowable;
    }
    
    ex->test = createBasicBlock("testException");
    
    // We can do this because readExceptionTable is the first function to be
    // called after creation of Opinfos
    for (uint16 i = ex->startpc; i < ex->endpc; ++i) {
      if (opcodeInfos[i].exceptionBlock == realEndExceptionBlock) {
        opcodeInfos[i].exceptionBlock = ex->test;
      }
    }

    if (!(opcodeInfos[ex->handlerpc].newBlock)) {
      opcodeInfos[ex->handlerpc].newBlock = createBasicBlock("handlerException");
    }
    
    ex->handler = opcodeInfos[ex->handlerpc].newBlock;
    opcodeInfos[ex->handlerpc].reqSuppl = true;

    exceptions.push_back(ex);
  }
  
  bool first = true;
  for (std::vector<Exception*>::iterator i = exceptions.begin(),
    e = exceptions.end(); i!= e; ++i) {

    Exception* cur = *i;
    Exception* next = 0;
    if (i + 1 != e) {
      next = *(i + 1);
    }

    if (first) {
      cur->realTest = createBasicBlock("realTestException");
    } else {
      cur->realTest = cur->test;
    }
    
    cur->exceptionPHI = new PHINode(mvm::jit::ptrType, "", cur->realTest);

    if (next && cur->startpc == next->startpc && cur->endpc == next->endpc)
      first = false;
    else
      first = true;
      
  }

  for (std::vector<Exception*>::iterator i = exceptions.begin(),
    e = exceptions.end(); i!= e; ++i) {

    Exception* cur = *i;
    Exception* next = 0;
    BasicBlock* bbNext = 0;
    PHINode* nodeNext = 0;
    if (i + 1 != e) {
      next = *(i + 1);
      if (!(cur->startpc >= next->startpc && cur->endpc <= next->endpc)) {
        bbNext = realEndExceptionBlock;
      } else {
        bbNext = next->realTest;
        nodeNext = next->exceptionPHI;
      }
    } else {
      bbNext = realEndExceptionBlock;
    }

    if (cur->realTest != cur->test) {
      const PointerType* PointerTy_0 = mvm::jit::ptrType;
      std::vector<Value*> int32_eh_select_params;
      Instruction* ptr_eh_ptr = new CallInst(mvm::jit::llvmGetException, "eh_ptr", cur->test);
      int32_eh_select_params.push_back(ptr_eh_ptr);
      int32_eh_select_params.push_back(ConstantExpr::getCast(Instruction::BitCast, mvm::jit::personality, PointerTy_0));
      int32_eh_select_params.push_back(mvm::jit::constantPtrNull);
      new CallInst(mvm::jit::exceptionSelector, int32_eh_select_params.begin(), int32_eh_select_params.end(), "eh_select", cur->test);
      new BranchInst(cur->realTest, cur->test);
      cur->exceptionPHI->addIncoming(ptr_eh_ptr, cur->test);
    } 

    Value* cl = new LoadInst(cur->catchClass->llvmVar(compilingClass->isolate->module), "", cur->realTest);
    Value* cmp = new CallInst(compareExceptionLLVM, cl, "", cur->realTest);
    new BranchInst(cur->handler, bbNext, cmp, cur->realTest);
    if (nodeNext)
      nodeNext->addIncoming(cur->exceptionPHI, cur->realTest);
    
    if (cur->handler->empty()) {
      cur->handlerPHI = new PHINode(mvm::jit::ptrType, "", cur->handler);
      cur->handlerPHI->addIncoming(cur->exceptionPHI, cur->realTest);
      Value* exc = new CallInst(getJavaExceptionLLVM, "", cur->handler);
      new CallInst(clearExceptionLLVM, "", cur->handler);
      new CallInst(mvm::jit::exceptionBeginCatch, cur->handlerPHI, "tmp8", cur->handler);
      std::vector<Value*> void_28_params;
      new CallInst(mvm::jit::exceptionEndCatch, void_28_params.begin(), void_28_params.end(), "", cur->handler);
      new StoreInst(exc, supplLocal, false, cur->handler);
    } else {
      Instruction* insn = cur->handler->begin();
      ((PHINode*)insn)->addIncoming(cur->exceptionPHI, cur->realTest);
    }
     
  }

  return nbe;

}

void JavaJIT::compareFP(Value* val1, Value* val2, const Type* ty, bool l) {
  Value* one = mvm::jit::constantOne;
  Value* zero = mvm::jit::constantZero;
  Value* minus = mvm::jit::constantMinusOne;

  Value* c = new FCmpInst(FCmpInst::FCMP_UGT, val1, val2, "", currentBlock);
  Value* r = new SelectInst(c, one, zero, "", currentBlock);
  c = new FCmpInst(FCmpInst::FCMP_ULT, val1, val2, "", currentBlock);
  r = new SelectInst(c, minus, r, "", currentBlock);
  c = new FCmpInst(FCmpInst::FCMP_UNO, val1, val2, "", currentBlock);
  r = new SelectInst(c, l ? one : minus, r, "", currentBlock);

  push(r, AssessorDesc::dInt);

}

void JavaJIT::_ldc(uint16 index) {
  JavaCtpInfo* ctpInfo = compilingClass->ctpInfo;
  uint8 type = ctpInfo->typeAt(index);
  
  if (type == JavaCtpInfo::ConstantString) {
    Value* toPush = 0;
    if (ctpInfo->ctpRes[index] == 0) {
      compilingClass->aquire();
      if (ctpInfo->ctpRes[index] == 0) {
        const UTF8* utf8 = ctpInfo->UTF8At(ctpInfo->ctpDef[index]);
        void* val = 0;
        GlobalVariable* gv = 0;
#ifndef SINGLE_VM
        if (compilingClass->isolate != Jnjvm::bootstrapVM) {
#endif
        val = compilingClass->isolate->UTF8ToStr(utf8);
        compilingClass->isolate->protectModule->lock();
        gv =
          new GlobalVariable(JavaObject::llvmType, false, 
                             GlobalValue::ExternalLinkage,
                             constantJavaObjectNull, "",
                             compilingClass->isolate->module);
        compilingClass->isolate->protectModule->unlock();
#ifndef SINGLE_VM
        } else {
          val = (void*)utf8;
          compilingClass->isolate->protectModule->lock();
          gv =
            new GlobalVariable(UTF8::llvmType, false, 
                               GlobalValue::ExternalLinkage,
                               constantUTF8Null, "",
                               compilingClass->isolate->module);
          compilingClass->isolate->protectModule->unlock();
        }
#endif
        
        // TODO: put an initializer in here
        void* ptr = mvm::jit::executionEngine->getPointerToGlobal(gv);
        GenericValue Val = GenericValue(val);
        llvm::GenericValue * Ptr = (llvm::GenericValue*)ptr;
        mvm::jit::executionEngine->StoreValueToMemory(Val, Ptr, JavaObject::llvmType);
        toPush = new LoadInst(gv, "", currentBlock);
        ctpInfo->ctpRes[index] = gv;
        compilingClass->release();
      } else {
        compilingClass->release();
        toPush = new LoadInst((GlobalVariable*)ctpInfo->ctpRes[index], "", currentBlock);
      }
    } else {
      toPush = new LoadInst((GlobalVariable*)ctpInfo->ctpRes[index], "", currentBlock);
    }
#ifndef SINGLE_VM
    if (compilingClass->isolate == Jnjvm::bootstrapVM)
      push(new CallInst(runtimeUTF8ToStrLLVM, toPush, "", currentBlock), AssessorDesc::dRef);
    else 
#endif
    push(toPush, AssessorDesc::dRef);
  } else if (type == JavaCtpInfo::ConstantLong) {
    mvm::jit::protectConstants();//->lock();
    push(ConstantInt::get(Type::Int64Ty, ctpInfo->LongAt(index)), AssessorDesc::dLong);
    mvm::jit::unprotectConstants();//->unlock();
  } else if (type == JavaCtpInfo::ConstantDouble) {
    mvm::jit::protectConstants();//->lock();
    push(ConstantFP::get(Type::DoubleTy, APFloat(ctpInfo->DoubleAt(index))), AssessorDesc::dDouble);
    mvm::jit::unprotectConstants();//->unlock();
  } else if (type == JavaCtpInfo::ConstantInteger) {
    mvm::jit::protectConstants();//->lock();
    push(ConstantInt::get(Type::Int32Ty, ctpInfo->IntegerAt(index)), AssessorDesc::dInt);
    mvm::jit::unprotectConstants();//->unlock();
  } else if (type == JavaCtpInfo::ConstantFloat) {
    mvm::jit::protectConstants();//->lock();
    push(ConstantFP::get(Type::FloatTy, APFloat(ctpInfo->FloatAt(index))), AssessorDesc::dFloat);
    mvm::jit::unprotectConstants();//->unlock();
  } else if (type == JavaCtpInfo::ConstantClass) {
    assert(0 && "implement ConstantClass in ldc!");
  } else {
    JavaThread::get()->isolate->unknownError("unknown type %d", type);
  }
}

void JavaJIT::JITVerifyNull(Value* obj) { 

  JavaJIT* jit = this;
  Constant* zero = constantJavaObjectNull;
  Value* test = new ICmpInst(ICmpInst::ICMP_EQ, obj, zero, "",
                             jit->currentBlock);

  BasicBlock* exit = jit->createBasicBlock("verifyNullExit");
  BasicBlock* cont = jit->createBasicBlock("verifyNullCont");

  new BranchInst(exit, cont, test, jit->currentBlock);
  std::vector<Value*> args;
  if (currentExceptionBlock != endExceptionBlock) {
    new InvokeInst(JavaJIT::nullPointerExceptionLLVM, unifiedUnreachable,
                   currentExceptionBlock, args.begin(),
                   args.end(), "", exit);
  } else {
    new CallInst(JavaJIT::nullPointerExceptionLLVM, args.begin(),
                 args.end(), "", exit);
    new UnreachableInst(exit);
  }
  

  jit->currentBlock = cont;
  
}

Value* JavaJIT::verifyAndComputePtr(Value* obj, Value* index,
                                    const Type* arrayType, bool verif) {
  JITVerifyNull(obj);
  
  if (index->getType() != Type::Int32Ty) {
    index = new SExtInst(index, Type::Int32Ty, "", currentBlock);
  }
  
  if (true) {
    Value* size = arraySize(obj);
    
    Value* cmp = new ICmpInst(ICmpInst::ICMP_ULT, index, size, "", currentBlock);

    BasicBlock* ifTrue =  createBasicBlock("true verifyAndComputePtr");
    BasicBlock* ifFalse = createBasicBlock("false verifyAndComputePtr");

    branch(cmp, ifTrue, ifFalse, currentBlock);
    
    std::vector<Value*>args;
    args.push_back(obj);
    args.push_back(index);
    if (currentExceptionBlock != endExceptionBlock) {
      new InvokeInst(JavaJIT::indexOutOfBoundsExceptionLLVM, unifiedUnreachable,
                     currentExceptionBlock, args.begin(),
                     args.end(), "", ifFalse);
    } else {
      new CallInst(JavaJIT::indexOutOfBoundsExceptionLLVM, args.begin(),
                   args.end(), "", ifFalse);
      new UnreachableInst(ifFalse);
    }
  
    currentBlock = ifTrue;
  }
  
  Constant* zero = mvm::jit::constantZero;
  Value* val = new BitCastInst(obj, arrayType, "", currentBlock);
  
  std::vector<Value*> indexes; //[3];
  indexes.push_back(zero);
  indexes.push_back(JavaArray::elementsOffset());
  indexes.push_back(index);
  Value* ptr = new GetElementPtrInst(val, indexes.begin(), indexes.end(), 
                                     "", currentBlock);

  return ptr;

}

void JavaJIT::setCurrentBlock(BasicBlock* newBlock) {

  std::vector< std::pair<Value*, const AssessorDesc*> > newStack;
  uint32 index = 0;
  for (BasicBlock::iterator i = newBlock->begin(), e = newBlock->end(); i != e;
       ++i, ++index) {
    // case 2 happens with handlers
    if (!(isa<PHINode>(i)) || i->getType() == mvm::jit::ptrType) {
      break;
    } else {
      const llvm::Type* type = i->getType();
      if (type == Type::Int32Ty || type == Type::Int16Ty || 
          type == Type::Int8Ty) {
        newStack.push_back(std::make_pair(i, AssessorDesc::dInt));
      } else {
        newStack.push_back(std::make_pair(i, stack[index].second));
      }
    }
  }
  
  stack = newStack;
  currentBlock = newBlock;
}

static void testPHINodes(BasicBlock* dest, BasicBlock* insert, JavaJIT* jit) {
  if(dest->empty()) {
    for (std::vector< std::pair<Value*, const AssessorDesc*> >::iterator i = jit->stack.begin(),
            e = jit->stack.end(); i!= e; ++i) {
      Value* cur = i->first;
      const AssessorDesc* func = i->second;
      PHINode* node = 0;
      if (func == AssessorDesc::dChar || func == AssessorDesc::dBool) {
        node = new PHINode(Type::Int32Ty, "", dest);
        cur = new ZExtInst(cur, Type::Int32Ty, "", jit->currentBlock);
      } else if (func == AssessorDesc::dByte || func == AssessorDesc::dShort) {
        node = new PHINode(Type::Int32Ty, "", dest);
        cur = new SExtInst(cur, Type::Int32Ty, "", jit->currentBlock);
      } else {
        node = new PHINode(cur->getType(), "", dest);
      }
      node->addIncoming(cur, insert);
    }
  } else {
    std::vector< std::pair<Value*, const AssessorDesc*> >::iterator stackit = jit->stack.begin();
    for (BasicBlock::iterator i = dest->begin(), e = dest->end(); i != e;
         ++i) {
      if (!(isa<PHINode>(i))) {
        break;
      } else {
        Instruction* ins = i;
        Value* cur = stackit->first;
        const AssessorDesc* func = stackit->second;
        
        if (func == AssessorDesc::dChar || func == AssessorDesc::dBool) {
          cur = new ZExtInst(cur, Type::Int32Ty, "", jit->currentBlock);
        } else if (func == AssessorDesc::dByte || func == AssessorDesc::dShort) {
          cur = new SExtInst(cur, Type::Int32Ty, "", jit->currentBlock);
        }
        
        ((PHINode*)ins)->addIncoming(cur, insert);
        ++stackit;
      }
    }
  }
}

void JavaJIT::branch(llvm::BasicBlock* dest, llvm::BasicBlock* insert) {
  testPHINodes(dest, insert, this);
  new BranchInst(dest, insert);
}

void JavaJIT::branch(llvm::Value* test, llvm::BasicBlock* ifTrue,
                     llvm::BasicBlock* ifFalse, llvm::BasicBlock* insert) {  
  testPHINodes(ifTrue, insert, this);
  testPHINodes(ifFalse, insert, this);
  new BranchInst(ifTrue, ifFalse, test, insert);
}

void JavaJIT::makeArgs(FunctionType::param_iterator it,
                       uint32 index, std::vector<Value*>& Args, uint32 nb) {
  Args.reserve(nb + 2);
  Value* args[nb];
  for (sint32 i = nb - 1; i >= 0; --i) {
    it--;
    if (it->get() == Type::Int64Ty || it->get() == Type::DoubleTy) {
      pop();
    }
    const AssessorDesc* func = topFunc();
    Value* tmp = pop();
    
    const Type* type = it->get();
    if (tmp->getType() != type) { // int8 or int16
      if (type == Type::Int32Ty) {
        if (func == AssessorDesc::dChar) {
          tmp = new ZExtInst(tmp, type, "", currentBlock);
        } else {
          tmp = new SExtInst(tmp, type, "", currentBlock);
        }
      } else {
        tmp = new TruncInst(tmp, type, "", currentBlock);
      }
    }
    args[i] = tmp;

  }

  for (uint32 i = 0; i < nb; ++i) {
    Args.push_back(args[i]);
  }
  
}

Instruction* JavaJIT::lowerMathOps(const UTF8* name, 
                                   std::vector<Value*>& args) {
  if (name == Jnjvm::abs) {
    const Type* Ty = args[0]->getType();
    if (Ty == Type::Int32Ty) {
      Constant* const_int32_9 = mvm::jit::constantZero;
      ConstantInt* const_int32_10 = mvm::jit::constantMinusOne;
      BinaryOperator* int32_tmpneg = BinaryOperator::create(Instruction::Sub, const_int32_9, args[0], "tmpneg", currentBlock);
      ICmpInst* int1_abscond = new ICmpInst(ICmpInst::ICMP_SGT, args[0], const_int32_10, "abscond", currentBlock);
      return new SelectInst(int1_abscond, args[0], int32_tmpneg, "abs", currentBlock);
    } else if (Ty == Type::Int64Ty) {
      Constant* const_int64_9 = mvm::jit::constantLongZero;
      ConstantInt* const_int64_10 = mvm::jit::constantLongMinusOne;
      BinaryOperator* int64_tmpneg = BinaryOperator::create(Instruction::Sub, const_int64_9, args[0], "tmpneg", currentBlock);
      ICmpInst* int1_abscond = new ICmpInst(ICmpInst::ICMP_SGT, args[0], const_int64_10, "abscond", currentBlock);
      return new SelectInst(int1_abscond, args[0], int64_tmpneg, "abs", currentBlock);
    } else if (Ty == Type::FloatTy) {
      return new CallInst(mvm::jit::func_llvm_fabs_f32, args[0], "tmp1", currentBlock);
    } else if (Ty == Type::DoubleTy) {
      return new CallInst(mvm::jit::func_llvm_fabs_f64, args[0], "tmp1", currentBlock);
    }
  } else if (name == Jnjvm::sqrt) {
    return new CallInst(mvm::jit::func_llvm_sqrt_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::sin) {
    return new CallInst(mvm::jit::func_llvm_sin_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::cos) {
    return new CallInst(mvm::jit::func_llvm_cos_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::tan) {
    return new CallInst(mvm::jit::func_llvm_tan_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::asin) {
    return new CallInst(mvm::jit::func_llvm_asin_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::acos) {
    return new CallInst(mvm::jit::func_llvm_acos_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::atan) {
    return new CallInst(mvm::jit::func_llvm_atan_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::atan2) {
    return new CallInst(mvm::jit::func_llvm_atan2_f64, args.begin(), args.end(), "tmp1", currentBlock);
  } else if (name == Jnjvm::exp) {
    return new CallInst(mvm::jit::func_llvm_exp_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::log) {
    return new CallInst(mvm::jit::func_llvm_log_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::pow) {
    return new CallInst(mvm::jit::func_llvm_pow_f64, args.begin(), args.end(), "tmp1", currentBlock);
  } else if (name == Jnjvm::ceil) {
    return new CallInst(mvm::jit::func_llvm_ceil_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::floor) {
    return new CallInst(mvm::jit::func_llvm_floor_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::rint) {
    return new CallInst(mvm::jit::func_llvm_rint_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::cbrt) {
    return new CallInst(mvm::jit::func_llvm_cbrt_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::cosh) {
    return new CallInst(mvm::jit::func_llvm_cosh_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::expm1) {
    return new CallInst(mvm::jit::func_llvm_expm1_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::hypot) {
    return new CallInst(mvm::jit::func_llvm_hypot_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::log10) {
    return new CallInst(mvm::jit::func_llvm_log10_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::log1p) {
    return new CallInst(mvm::jit::func_llvm_log1p_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::sinh) {
    return new CallInst(mvm::jit::func_llvm_sinh_f64, args[0], "tmp1", currentBlock);
  } else if (name == Jnjvm::tanh) {
    return new CallInst(mvm::jit::func_llvm_tanh_f64, args[0], "tmp1", currentBlock);
  }
  
  return 0;

}


Instruction* JavaJIT::invokeInline(JavaMethod* meth, 
                                   std::vector<Value*>& args) {
  JavaJIT* jit = gc_new(JavaJIT)();
  jit->compilingClass = meth->classDef; 
  jit->compilingMethod = meth;
  jit->unifiedUnreachable = unifiedUnreachable;
  jit->inlineMethods = inlineMethods;
  jit->inlineMethods[meth] = true;
  Instruction* ret = jit->inlineCompile(llvmFunction, currentBlock, 
                                        currentExceptionBlock, args);
  inlineMethods[meth] = false;
  return ret;
}

void JavaJIT::invokeSpecial(uint16 index) {
  JavaCtpInfo* ctpInfo = compilingClass->ctpInfo;
  JavaMethod* meth = 0;
  Signdef* signature = 0;
  const UTF8* name = 0;
  const UTF8* cl = 0;
  ctpInfo->nameOfStaticOrSpecialMethod(index, cl, name, signature);
  llvm::Instruction* val = 0;
    
  std::vector<Value*> args; 
  FunctionType::param_iterator it  = signature->virtualType->param_end();
  makeArgs(it, index, args, signature->nbIn + 1);
  JITVerifyNull(args[0]); 

  if (cl == Jnjvm::mathName) {
    val = lowerMathOps(name, args);
  }

  if (!val) {
    Function* func = ctpInfo->infoOfStaticOrSpecialMethod(index, ACC_VIRTUAL,
                                                          signature, meth);

    if (meth && meth->canBeInlined && meth != compilingMethod && 
        inlineMethods[meth] == 0) {
      val = invokeInline(meth, args);
    } else {
      val = invoke(func, args, "", currentBlock);
    }
  }
  
  const llvm::Type* retType = signature->virtualType->getReturnType();
  if (retType != Type::VoidTy) {
    push(val, signature->ret->funcs);
    if (retType == Type::DoubleTy || retType == Type::Int64Ty) {
      push(mvm::jit::constantZero, AssessorDesc::dInt);
    }
  }

}

void JavaJIT::invokeStatic(uint16 index) {
  JavaCtpInfo* ctpInfo = compilingClass->ctpInfo;
  JavaMethod* meth = 0;
  Signdef* signature = 0;
  const UTF8* name = 0;
  const UTF8* cl = 0;
  ctpInfo->nameOfStaticOrSpecialMethod(index, cl, name, signature);
  llvm::Instruction* val = 0;
  
  std::vector<Value*> args; // size = [signature->nbIn + 2]; 
  FunctionType::param_iterator it  = signature->staticType->param_end();
  makeArgs(it, index, args, signature->nbIn);
  ctpInfo->markAsStaticCall(index);

  if (cl == Jnjvm::mathName) {
    val = lowerMathOps(name, args);
  }

  if (!val) {
    Function* func = ctpInfo->infoOfStaticOrSpecialMethod(index, ACC_STATIC,
                                                          signature, meth);

    if (meth && meth->canBeInlined && meth != compilingMethod && 
        inlineMethods[meth] == 0) {
      val = invokeInline(meth, args);
    } else {
      val = invoke(func, args, "", currentBlock);
    }
  }

  const llvm::Type* retType = signature->staticType->getReturnType();
  if (retType != Type::VoidTy) {
    push(val, signature->ret->funcs);
    if (retType == Type::DoubleTy || retType == Type::Int64Ty) {
      push(mvm::jit::constantZero, AssessorDesc::dInt);
    }
  }
}
    
Value* JavaJIT::getInitializedClass(uint16 index) {
    const Type* PtrTy = mvm::jit::ptrType;
    compilingClass->isolate->protectModule->lock();
    GlobalVariable * gv =
      new GlobalVariable(PtrTy, false, 
                         GlobalValue::ExternalLinkage,
                         mvm::jit::constantPtrNull, "",
                         compilingClass->isolate->module);

    compilingClass->isolate->protectModule->unlock();
    
    Value* arg1 = new LoadInst(gv, "", false, currentBlock);
    Value* test = new ICmpInst(ICmpInst::ICMP_EQ, arg1, 
                               mvm::jit::constantPtrNull, "", currentBlock);
    
    BasicBlock* trueCl = createBasicBlock("Cl OK");
    BasicBlock* falseCl = createBasicBlock("Cl Not OK");
    PHINode* node = new PHINode(PtrTy, "", trueCl);
    node->addIncoming(arg1, currentBlock);
    new BranchInst(falseCl, trueCl, test, currentBlock);

    currentBlock = falseCl;

    std::vector<Value*> Args;
    Value* v = 
      new LoadInst(compilingClass->llvmVar(compilingClass->isolate->module), 
                   "", currentBlock);
    Args.push_back(v);
    mvm::jit::protectConstants();
    ConstantInt* CI = ConstantInt::get(Type::Int32Ty, index);
    mvm::jit::unprotectConstants();
    Args.push_back(CI);
    Args.push_back(gv);
    Value* res = invoke(newLookupLLVM, Args, "", currentBlock);
    node->addIncoming(res, currentBlock);

    new BranchInst(trueCl, currentBlock);
    currentBlock = trueCl;
    
    return node;
}

void JavaJIT::invokeNew(uint16 index) {
  JavaCtpInfo* ctpInfo = compilingClass->ctpInfo;
  ctpInfo->checkInfoOfClass(index);
  
  Class* cl = (Class*)(ctpInfo->getMethodClassIfLoaded(index));
  Value* val = 0;
  if (!cl || !cl->isReady()) {
    Value* node = getInitializedClass(index);
    val = invoke(doNewUnknownLLVM, node, "", currentBlock);
  } else {
    Value* load = new LoadInst(cl->llvmVar(compilingClass->isolate->module), "", currentBlock);
    val = invoke(doNewLLVM, load, "", currentBlock);
    // give the real type info, escape analysis uses it
    new BitCastInst(val, cl->virtualType, "", currentBlock);
  }
  
  push(val, AssessorDesc::dRef);

}

Value* JavaJIT::arraySize(Value* val) {
  return new CallInst(arrayLengthLLVM, val, "", currentBlock);
  /*
  Value* array = new BitCastInst(val, JavaArray::llvmType, "", currentBlock);
  std::vector<Value*> args; //size=  2
  args.push_back(mvm::jit::constantZero);
  args.push_back(JavaArray::sizeOffset());
  Value* ptr = new GetElementPtrInst(array, args.begin(), args.end(),
                                     "", currentBlock);
  return new LoadInst(ptr, "", currentBlock);*/
}

static llvm::Value* fieldGetter(JavaJIT* jit, const Type* type, Value* object,
                                Value* offset) {
  llvm::Value* objectConvert = new BitCastInst(object, type, "",
                                               jit->currentBlock);

  Constant* zero = mvm::jit::constantZero;
  std::vector<Value*> args; // size = 2
  args.push_back(zero);
  args.push_back(offset);
  llvm::Value* ptr = new GetElementPtrInst(objectConvert, args.begin(),
                                           args.end(), "", jit->currentBlock);
  return ptr;  
}

Value* JavaJIT::ldResolved(uint16 index, bool stat, Value* object, 
                           const Type* fieldType, const Type* fieldTypePtr) {
  JavaCtpInfo* info = compilingClass->ctpInfo;
  
  JavaField* field = info->lookupField(index, stat);
  if (field && field->classDef->isReady()) {
    if (stat) object = new LoadInst(field->classDef->staticVar(compilingClass->isolate->module), "",
                                    currentBlock);
    const Type* type = stat ? field->classDef->staticType :
                              field->classDef->virtualType;

#ifndef SINGLE_VM
    if (stat && field->classDef->isolate == Jnjvm::bootstrapVM) {
      object = new CallInst(getStaticInstanceLLVM, object, "", currentBlock);
    }
#endif
    return fieldGetter(this, type, object, field->offset);
  } else {
    const Type* Pty = mvm::jit::arrayPtrType;
    compilingClass->isolate->protectModule->lock();
    GlobalVariable* gvStaticInstance = 
      new GlobalVariable(mvm::jit::ptrType, false, 
                         GlobalValue::ExternalLinkage,
                         mvm::jit::constantPtrNull, 
                         "", compilingClass->isolate->module);
    
    
    Constant* zero = mvm::jit::constantZero;
    GlobalVariable* gv = 
      new GlobalVariable(Type::Int32Ty, false, GlobalValue::ExternalLinkage,
                         zero, "", compilingClass->isolate->module);
    compilingClass->isolate->protectModule->unlock();
    
    // set is volatile
    Value* val = new LoadInst(gv, "", true, currentBlock);
    Value * cmp = new ICmpInst(ICmpInst::ICMP_NE, val, zero, "", currentBlock);
    BasicBlock* ifTrue  = createBasicBlock("true ldResolved");
    BasicBlock* ifFalse  = createBasicBlock("false ldResolved");
    BasicBlock* endBlock  = createBasicBlock("end ldResolved");
    PHINode * node = new PHINode(mvm::jit::ptrType, "", endBlock);
    new BranchInst(ifTrue, ifFalse, cmp, currentBlock);
  
    // ---------- In case we already resolved something --------------------- //
    currentBlock = ifTrue;
    Value* resPtr = 0;
    if (object) {
      Value* ptr = new BitCastInst(object, Pty, "", currentBlock);
      std::vector<Value*> gepArgs; // size = 1
      gepArgs.push_back(zero);
      gepArgs.push_back(val);
      resPtr = new GetElementPtrInst(ptr, gepArgs.begin(), gepArgs.end(),
                                     "", currentBlock);
    
    } else {
      resPtr = new LoadInst(gvStaticInstance, "", currentBlock);
    }
    
    node->addIncoming(resPtr, currentBlock);
    new BranchInst(endBlock, currentBlock);

    // ---------- In case we have to resolve -------------------------------- //
    currentBlock = ifFalse;
    std::vector<Value*> args;
    if (object) {
      args.push_back(object);
    } else {
      args.push_back(constantJavaObjectNull);
    }
    args.push_back(new LoadInst(compilingClass->llvmVar(compilingClass->isolate->module), "",
                                currentBlock));
    mvm::jit::protectConstants();//->lock();
    args.push_back(ConstantInt::get(Type::Int32Ty, index));
    mvm::jit::unprotectConstants();//->unlock();
    args.push_back(stat ? mvm::jit::constantOne : mvm::jit::constantZero);
    args.push_back(gvStaticInstance);
    args.push_back(gv);
    Value* tmp = invoke(JavaJIT::fieldLookupLLVM, args, "", currentBlock);
    node->addIncoming(tmp, currentBlock);
    new BranchInst(endBlock, currentBlock);
    
    currentBlock = endBlock;;
    return new BitCastInst(node, fieldTypePtr, "", currentBlock);
  }
}

extern void convertValue(Value*& val, const Type* t1, BasicBlock* currentBlock, bool usign);
 

void JavaJIT::setStaticField(uint16 index) {
  const AssessorDesc* ass = topFunc();
  Value* val = pop(); 
  Typedef* sign = compilingClass->ctpInfo->infoOfField(index);
  const Type* type = sign->funcs->llvmType;
  if (type == Type::Int64Ty || type == Type::DoubleTy) {
    val = pop();
  }
  Value* ptr = ldResolved(index, true, 0, type, sign->funcs->llvmTypePtr);
  
  if (type != val->getType()) { // int1, int8, int16
    convertValue(val, type, currentBlock, 
                 ass == AssessorDesc::dChar || ass == AssessorDesc::dBool);
  }
  
  new StoreInst(val, ptr, false, currentBlock);
}

void JavaJIT::getStaticField(uint16 index) {
  Typedef* sign = compilingClass->ctpInfo->infoOfField(index);
  Value* ptr = ldResolved(index, true, 0, sign->funcs->llvmType, 
                          sign->funcs->llvmTypePtr);
  push(new LoadInst(ptr, "", currentBlock), sign->funcs);
  const Type* type = sign->funcs->llvmType;
  if (type == Type::Int64Ty || type == Type::DoubleTy) {
    push(mvm::jit::constantZero, AssessorDesc::dInt);
  }
}

void JavaJIT::setVirtualField(uint16 index) {
  const AssessorDesc* ass = topFunc();
  Value* val = pop();
  Typedef* sign = compilingClass->ctpInfo->infoOfField(index);
  const Type* type = sign->funcs->llvmType;
  
  if (type == Type::Int64Ty || type == Type::DoubleTy) {
    val = pop();
  }
  
  Value* object = pop();
  JITVerifyNull(object);
  Value* ptr = ldResolved(index, false, object, type, 
                          sign->funcs->llvmTypePtr);

  if (type != val->getType()) { // int1, int8, int16
    convertValue(val, type, currentBlock, 
                 ass == AssessorDesc::dChar || ass == AssessorDesc::dBool);
  }

  new StoreInst(val, ptr, false, currentBlock);
}

void JavaJIT::getVirtualField(uint16 index) {
  Typedef* sign = compilingClass->ctpInfo->infoOfField(index);
  Value* obj = pop();
  JITVerifyNull(obj);
  Value* ptr = ldResolved(index, false, obj, sign->funcs->llvmType, 
                          sign->funcs->llvmTypePtr);
  push(new LoadInst(ptr, "", currentBlock), sign->funcs);
  const Type* type = sign->funcs->llvmType;
  if (type == Type::Int64Ty || type == Type::DoubleTy) {
    push(mvm::jit::constantZero, AssessorDesc::dInt);
  }
}


Instruction* JavaJIT::invoke(Value *F, std::vector<llvm::Value*>& args,
                       const char* Name,
                       BasicBlock *InsertAtEnd) {
  
  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    return new InvokeInst(F, ifNormal, currentExceptionBlock, args.begin(), 
                          args.end(), Name, InsertAtEnd);
  } else {
    return new CallInst(F, args.begin(), args.end(), Name, InsertAtEnd);
  }
}

Instruction* JavaJIT::invoke(Value *F, Value* arg1, const char* Name,
                       BasicBlock *InsertAtEnd) {

  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    std::vector<Value*> arg;
    arg.push_back(arg1);
    return new InvokeInst(F, ifNormal, currentExceptionBlock, arg.begin(),
                          arg.end(), Name, InsertAtEnd);
  } else {
    return new CallInst(F, arg1, Name, InsertAtEnd);
  }
}

Instruction* JavaJIT::invoke(Value *F, Value* arg1, Value* arg2,
                       const char* Name, BasicBlock *InsertAtEnd) {

  std::vector<Value*> args;
  args.push_back(arg1);
  args.push_back(arg2);
  
  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    return new InvokeInst(F, ifNormal, currentExceptionBlock, args.begin(),
                          args.end(), Name, InsertAtEnd);
  } else {
    return new CallInst(F, args.begin(), args.end(), Name, InsertAtEnd);
  }
}

Instruction* JavaJIT::invoke(Value *F, const char* Name,
                       BasicBlock *InsertAtEnd) {
  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    std::vector<Value*> args;
    return new InvokeInst(F, ifNormal, currentExceptionBlock, args.begin(),
                          args.end(), Name, InsertAtEnd);
  } else {
    return new CallInst(F, Name, InsertAtEnd);
  }
}


