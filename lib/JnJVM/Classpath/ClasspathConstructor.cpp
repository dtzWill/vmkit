//===- ClasspathConstructor.cpp -------------------------------------------===//
//===----------- GNU classpath java/lang/reflect/Constructor --------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <string.h>

#include "types.h"

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaObject.h"
#include "JavaTypes.h"
#include "JavaThread.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "JnjvmClassLoader.h"
#include "NativeUtil.h"

using namespace jnjvm;

extern "C" {

// internalGetClass selects the class of a method depending on the isolate
// environment. In a sharing environment, the class is located in the 
// Java object. In regular environment, it is the classDef of the method.
static UserClass* internalGetClass(Jnjvm* vm, JavaMethod* meth, jobject Meth) {
#ifdef ISOLATE_SHARING
  JavaField* field = vm->upcalls->constructorClass;
  jclass Cl = (jclass)field->getInt32Field((JavaObject*)Meth);
  UserClass* cl = (UserClass*)NativeUtil::resolvedImplClass(vm, Cl, false);
  return cl;
#else
  return meth->classDef;
#endif
}

JNIEXPORT jobject JNICALL Java_java_lang_reflect_Constructor_getParameterTypes(
#ifdef NATIVE_JNI
JNIEnv *env,
#endif
jobject cons) {
  verifyNull(cons);
  Jnjvm* vm = JavaThread::get()->isolate;
  JavaField* field = vm->upcalls->constructorSlot;
  JavaMethod* meth = (JavaMethod*)(field->getInt32Field((JavaObject*)cons));
  UserClass* cl = internalGetClass(vm, meth, cons);
  JnjvmClassLoader* loader = cl->classLoader;

  return (jobject)(NativeUtil::getParameterTypes(loader, meth));
}

JNIEXPORT jint JNICALL Java_java_lang_reflect_Constructor_getModifiersInternal(
#ifdef NATIVE_JNI
JNIEnv *env,
#endif
jobject cons) {
  verifyNull(cons);
  Jnjvm* vm = JavaThread::get()->isolate;
  JavaField* field = vm->upcalls->constructorSlot;
  JavaMethod* meth = (JavaMethod*)(field->getInt32Field((JavaObject*)cons));
  return meth->access;
}

JNIEXPORT jobject JNICALL Java_java_lang_reflect_Constructor_constructNative(
#ifdef NATIVE_JNI
JNIEnv *env,
#endif
jobject _cons,
jobject _args, 
jclass Clazz, 
jint _meth) {
  
  Jnjvm* vm = JavaThread::get()->isolate;
  JavaMethod* meth = (JavaMethod*)_meth;
  JavaArray* args = (JavaArray*)_args;
  sint32 nbArgs = args ? args->size : 0;
  sint32 size = meth->getSignature()->args.size();

  // Allocate a buffer to store the arguments.
  void** buf = (void**)alloca(size * sizeof(uint64));
  // Record the beginning of the buffer.
  void* startBuf = (void*)buf;

  sint32 index = 0;
  if (nbArgs == size) {
    UserCommonClass* _cl = NativeUtil::resolvedImplClass(vm, Clazz, false);
    UserClass* cl = _cl->asClass();
    if (cl) {
      cl->initialiseClass(vm);
      JavaObject* res = cl->doNew(vm);
      JavaObject** ptr = (JavaObject**)(void*)(args->elements);
      Signdef* sign = meth->getSignature();
      
      // Store the arguments, unboxing primitives if necessary.
      for (std::vector<Typedef*>::iterator i = sign->args.begin(),
           e = sign->args.end(); i != e; ++i, ++index) {
        NativeUtil::decapsulePrimitive(vm, buf, ptr[index], *i);
      }
      
      JavaObject* excp = 0;
      try {
        meth->invokeIntSpecialBuf(vm, cl, res, startBuf);
      }catch(...) {
        excp = JavaThread::getJavaException();
        JavaThread::clearException();
      }
      if (excp) {
        if (excp->classOf->isAssignableFrom(vm->upcalls->newException)) {
          // If it's an exception, we encapsule it in an
          // invocationTargetException
          vm->invocationTargetException(excp);
        } else {
          // If it's an error, throw it again.
          JavaThread::throwException(excp);
        }
      }
    
      return (jobject)res;
    }
  }
  vm->illegalArgumentExceptionForMethod(meth, 0, 0); 
  return 0;
}

JNIEXPORT 
jobjectArray JNICALL Java_java_lang_reflect_Constructor_getExceptionTypes(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
jobject cons) {
  verifyNull(cons);
  Jnjvm* vm = JavaThread::get()->isolate;
  JavaField* field = vm->upcalls->constructorSlot;
  JavaMethod* meth = (JavaMethod*)field->getInt32Field((JavaObject*)cons);
  UserClass* cl = internalGetClass(vm, meth, cons);

  return (jobjectArray)NativeUtil::getExceptionTypes(cl, meth);
}

}
