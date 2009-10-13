//===--- PNetString.cpp - Implementation of PNet string interface ---------===//
//
//                                N3
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/LLVMContext.h"

#include "mvm/JIT.h"

#include "CLIString.h"
#include "MSCorlib.h"
#include "N3.h"
#include "MonoString.h"
#include "VMArray.h"
#include "VMClass.h"
#include "VMThread.h"

using namespace n3;
using namespace llvm;


CLIString* CLIString::stringDup(const ArrayChar*& array, N3* vm) {
  MonoString* obj = (MonoString*)(*MSCorlib::pString)();
  obj->length = array->size;
  if (array->size == 0) {
    obj->startChar = 0;
  } else {
    obj->startChar = array->elements[0];
  }
  obj->value = array; 
  return obj;
}

const ArrayChar* CLIString::strToArray(N3* vm) const {
  return ((MonoString *)this)->value;
}

GlobalVariable* CLIString::llvmVar() {
  MonoString* str = (MonoString*)this;
  if (!str->_llvmVar) {
    N3* vm = VMThread::get()->getVM();
    if (!str->_llvmVar) {
      const Type* pty = mvm::MvmModule::ptrType;
      Module* Mod = vm->getLLVMModule();
      Constant* cons = 
        ConstantExpr::getIntToPtr(ConstantInt::get(Type::getInt64Ty(getGlobalContext()), uint64_t (this)),
                                  pty);
      str->_llvmVar = new GlobalVariable(*Mod, pty, true,
                                    GlobalValue::ExternalLinkage,
                                    cons, "");
    }
  }
  return str->_llvmVar;
}
