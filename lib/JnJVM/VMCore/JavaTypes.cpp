//===------------- JavaTypes.cpp - Java primitives ------------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <vector>

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaCompiler.h"
#include "JavaTypes.h"

using namespace jnjvm;

UserCommonClass* ArrayTypedef::assocClass(JnjvmClassLoader* loader) const {
  return loader->constructArray(keyName);
}

UserCommonClass* ObjectTypedef::assocClass(JnjvmClassLoader* loader) const {
  return loader->loadName(pseudoAssocClassName, false, true);
}

UserCommonClass* ObjectTypedef::findAssocClass(JnjvmClassLoader* loader) const {
  return loader->lookupClassOrArray(pseudoAssocClassName);
}

Signdef::Signdef(const UTF8* name, JnjvmClassLoader* loader,
                 std::vector<Typedef*>& args, Typedef* ret) {
  
  arguments[0] = ret;
  Typedef** myArgs = &(arguments[1]);
  nbArguments = args.size();
  uint32 index = 0;
  for (std::vector<Typedef*>::iterator i = args.begin(), e = args.end();
       i != e; ++i) {
    myArgs[index++] = *i;
  }
  initialLoader = loader;
  keyName = name;
  JInfo = 0;
  _virtualCallBuf = 0;
  _staticCallBuf = 0;
  _virtualCallAP = 0;
  _staticCallAP = 0;
  
}

ObjectTypedef::ObjectTypedef(const UTF8* name, UTF8Map* map) {
  keyName = name;
  pseudoAssocClassName = name->extract(map, 1, name->size - 1);
}

intptr_t Signdef::staticCallBuf() {
  if (!_staticCallBuf) {
    char* buf = (char*)alloca((keyName->size << 1) + 1 + 11);
    nativeName(buf, "static_buf");
    bool unused = false;
    intptr_t res = initialLoader->loadInLib(buf, unused);
    if (res) {
      _staticCallBuf = res;
    } else {
      initialLoader->getCompiler()->staticCallBuf(this);
    }
  }
  return _staticCallBuf;
}

intptr_t Signdef::virtualCallBuf() {
  if (!_virtualCallBuf) {
    char* buf = (char*)alloca((keyName->size << 1) + 1 + 11);
    nativeName(buf, "virtual_buf");
    bool unused = false;
    intptr_t res = initialLoader->loadInLib(buf, unused);
    if (res) { 
      _virtualCallBuf = res;
    } else {
      initialLoader->getCompiler()->virtualCallBuf(this);
    }
  }
  return _virtualCallBuf;
}

intptr_t Signdef::staticCallAP() {
  if (!_staticCallAP) {
    char* buf = (char*)alloca((keyName->size << 1) + 1 + 11);
    nativeName(buf, "static_ap");
    bool unused = false;
    intptr_t res = initialLoader->loadInLib(buf, unused);
    if (res) {
      _staticCallAP = res;
    } else {
      initialLoader->getCompiler()->staticCallAP(this);
    }
  }
  return _staticCallAP;
}

intptr_t Signdef::virtualCallAP() {
  if (!_virtualCallAP) {
    char* buf = (char*)alloca((keyName->size << 1) + 1 + 11);
    nativeName(buf, "virtual_ap");
    bool unused = false;
    intptr_t res = initialLoader->loadInLib(buf, unused);
    if (res) {
      _virtualCallAP = res;
    } else {
      initialLoader->getCompiler()->virtualCallAP(this);
    }
  }
  return _virtualCallAP;
}


void Signdef::nativeName(char* ptr, const char* ext) const {
  sint32 i = 0;
  while (i < keyName->size) {
    char c = keyName->elements[i++];
    if (c == I_PARG) {
      ptr[0] = '_';
      ptr[1] = '_';
      ptr += 2;
    } else if (c == '/') {
      ptr[0] = '_';
      ++ptr;
    } else if (c == '_') {
      ptr[0] = '_';
      ptr[1] = '1';
      ptr += 2;
    } else if (c == I_END_REF) {
      ptr[0] = '_';
      ptr[1] = '2';
      ptr += 2;
    } else if (c == I_TAB) {
      ptr[0] = '_';
      ptr[1] = '3';
      ptr += 2;
    } else if (c == I_PARD) {
      break;
    } else {
      ptr[0] = c;
      ++ptr;
    }
  }

  assert(ext && "I need an extension");
  memcpy(ptr, ext, strlen(ext) + 1);
}
