//===- ClasspathConstructor.cpp -------------------------------------------===//
//===----------- GNU classpath java/lang/reflect/Constructor --------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "types.h"

#include "Classpath.h"
#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaObject.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "JnjvmClassLoader.h"

using namespace jnjvm;

extern "C" {

static UserClass* internalGetClass(Jnjvm* vm, jobject Meth) {
  JavaField* field = vm->upcalls->constructorClass;
  JavaObject* Cl = (JavaObject*)field->getObjectField((JavaObject*)Meth);
  UserClass* cl = (UserClass*)UserCommonClass::resolvedImplClass(vm, Cl, false);
  return cl;
}

JNIEXPORT jobject JNICALL Java_java_lang_reflect_Constructor_getParameterTypes(
#ifdef NATIVE_JNI
JNIEnv *env,
#endif
jobject cons) {

  jobject res = 0;

  BEGIN_NATIVE_EXCEPTION(0)

  verifyNull(cons);
  Jnjvm* vm = JavaThread::get()->getJVM();
  UserClass* cl = internalGetClass(vm, cons);
  JavaField* field = vm->upcalls->constructorSlot;
  uint32 index = (uint32)(field->getInt32Field((JavaObject*)cons));
  JavaMethod* meth = &(cl->virtualMethods[index]);
  JnjvmClassLoader* loader = cl->classLoader;

  res = (jobject)meth->getParameterTypes(loader);
  END_NATIVE_EXCEPTION

  return res;
}

JNIEXPORT jint JNICALL Java_java_lang_reflect_Constructor_getModifiersInternal(
#ifdef NATIVE_JNI
JNIEnv *env,
#endif
jobject cons) {

  jint res = 0;

  BEGIN_NATIVE_EXCEPTION(0)

  verifyNull(cons);
  Jnjvm* vm = JavaThread::get()->getJVM();
  UserClass* cl = internalGetClass(vm, cons);
  JavaField* field = vm->upcalls->constructorSlot;
  uint32 index = (uint32)(field->getInt32Field((JavaObject*)cons));
  JavaMethod* meth = &(cl->virtualMethods[index]);
  res = meth->access;

  END_NATIVE_EXCEPTION

  return res;
}

JNIEXPORT jobject JNICALL Java_java_lang_reflect_Constructor_constructNative(
#ifdef NATIVE_JNI
JNIEnv *env,
#endif
jobject _cons, jobject _args, jclass Clazz, jint index) {

  jobject res = 0;

  BEGIN_NATIVE_EXCEPTION(0)
  
  Jnjvm* vm = JavaThread::get()->getJVM();
  UserClass* cl = internalGetClass(vm, _cons);
  JavaMethod* meth = &(cl->virtualMethods[index]);
  JavaArray* args = (JavaArray*)_args;
  sint32 nbArgs = args ? args->size : 0;
  Signdef* sign = meth->getSignature();
  sint32 size = sign->nbArguments;

  // Allocate a buffer to store the arguments.
  uintptr_t buf = (uintptr_t)alloca(size * sizeof(uint64));
  // Record the beginning of the buffer.
  void* startBuf = (void*)buf;

  if (nbArgs == size) {
    UserCommonClass* _cl = 
      UserCommonClass::resolvedImplClass(vm, (JavaObject*)Clazz, false);
    UserClass* cl = _cl->asClass();
    if (cl) {
      cl->initialiseClass(vm);
      JavaObject* obj = cl->doNew(vm);
      res = (jobject) obj;
      JavaObject** ptr = (JavaObject**)(void*)(args->elements);
      
      Typedef* const* arguments = sign->getArgumentsType();
      // Store the arguments, unboxing primitives if necessary.
      for (sint32 i = 0; i < size; ++i) {
        ptr[i]->decapsulePrimitive(vm, buf, arguments[i]);
      }
      
      JavaObject* excp = 0;
      JavaThread* th = JavaThread::get();
      try {
        meth->invokeIntSpecialBuf(vm, cl, obj, startBuf);
      }catch(...) {
        excp = th->getJavaException();
        th->clearException();
      }
      if (excp) {
        if (excp->classOf->isAssignableFrom(vm->upcalls->newException)) {
          // If it's an exception, we encapsule it in an
          // invocationTargetException
          vm->invocationTargetException(excp);
        } else {
          // If it's an error, throw it again.
          th->throwException(excp);
        }
      }
    
    } else {
      vm->illegalArgumentExceptionForMethod(meth, 0, 0);
    }
  } else {
    vm->illegalArgumentExceptionForMethod(meth, 0, 0);
  }
  
  END_NATIVE_EXCEPTION
  
  return res;
}

JNIEXPORT 
jobjectArray JNICALL Java_java_lang_reflect_Constructor_getExceptionTypes(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
jobject cons) {
  
  jobjectArray res = 0;

  BEGIN_NATIVE_EXCEPTION(0)
  
  verifyNull(cons);
  Jnjvm* vm = JavaThread::get()->getJVM();
  UserClass* cl = internalGetClass(vm, cons);
  JavaField* field = vm->upcalls->constructorSlot;
  uint32 index = (uint32)(field->getInt32Field((JavaObject*)cons));
  JavaMethod* meth = &(cl->virtualMethods[index]);
  JnjvmClassLoader* loader = cl->classLoader;

  res = (jobjectArray)meth->getExceptionTypes(loader);

  END_NATIVE_EXCEPTION

  return res;
}

}
