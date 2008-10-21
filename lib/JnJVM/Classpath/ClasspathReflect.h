//===------ ClasspathReflect.h - GNU classpath definitions of ----------------//
// java/lang/Class, java/lang/reflect/Field, java/lang/reflect/Method and ----//
// java/lang/reflect/Constructor as compiled by JnJVM. -----------------------//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef JNJVM_CLASSPATH_REFLECT_H
#define JNJVM_CLASSPATH_REFLECT_H

#include "MvmGC.h"

#include <JavaClass.h>
#include <JavaObject.h>

namespace jnjvm {

class JavaObjectClass : public JavaObject {
private:
  JavaObject* signers;
  JavaObject* pd;
  UserCommonClass* vmdata;
  JavaObject* constructor;

public:
  
  UserCommonClass* getClass() {
    return vmdata;
  }

  static void STATIC_TRACER(JavaObjectClass) {
    obj->JavaObject::CALL_TRACER;
    obj->pd->MARK_AND_TRACE;
    obj->signers->MARK_AND_TRACE;
    obj->constructor->MARK_AND_TRACE;
    if (obj->vmdata) obj->vmdata->classLoader->MARK_AND_TRACE;
  }
};

class JavaObjectField : public JavaObject {
private:
  uint8 flag;
  JavaObject* declaringClass;
  JavaObject* name;
  JavaField* slot;

public:

  static void STATIC_TRACER(JavaObjectField) {
    obj->JavaObject::CALL_TRACER;
    obj->name->MARK_AND_TRACE;
    obj->declaringClass->MARK_AND_TRACE;
    // No need to see if classDef != NULL, it must be.
    if (obj->slot) obj->slot->classDef->classLoader->MARK_AND_TRACE;
  }

};

class JavaObjectMethod : public JavaObject {
private:
  uint8 flag;
  JavaObject* declaringClass;
  JavaObject* name;
  JavaMethod* slot;

public:
  
  static void STATIC_TRACER(JavaObjectMethod) {
    obj->JavaObject::CALL_TRACER;
    obj->name->MARK_AND_TRACE;
    obj->declaringClass->MARK_AND_TRACE;
    if (obj->slot) obj->slot->classDef->classLoader->MARK_AND_TRACE;
  }

};

class JavaObjectConstructor : public JavaObject {
private:
  uint8 flag;
  JavaObject* clazz;
  JavaMethod* slot;

public:
  static void STATIC_TRACER(JavaObjectConstructor) {
    obj->JavaObject::CALL_TRACER;
    obj->clazz->MARK_AND_TRACE;
    if (obj->slot) obj->slot->classDef->classLoader->MARK_AND_TRACE;
  }

};

}

#endif
