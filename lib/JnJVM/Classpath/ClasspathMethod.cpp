//===- ClasspathMethod.cpp ------------------------------------------------===//
//===------------- GNU classpath java/lang/reflect/Method -----------------===//
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
  JavaField* field = vm->upcalls->methodClass;
  jclass Cl = (jclass)field->getInt32Field((JavaObject*)Meth);
  UserClass* cl = (UserClass*)NativeUtil::resolvedImplClass(vm, Cl, false);
  return cl;
#else
  return meth->classDef;
#endif
}


JNIEXPORT jint JNICALL Java_java_lang_reflect_Method_getModifiersInternal(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
 jobject Meth) { 
  Jnjvm* vm = JavaThread::get()->getJVM();
  JavaField* slot = vm->upcalls->methodSlot;
  JavaMethod* meth = (JavaMethod*)slot->getInt32Field((JavaObject*)Meth);
  return meth->access;
}

JNIEXPORT jclass JNICALL Java_java_lang_reflect_Method_getReturnType(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
 jobject Meth) {
  Jnjvm* vm = JavaThread::get()->getJVM();
  JavaField* slot = vm->upcalls->methodSlot;
  JavaMethod* meth = (JavaMethod*)slot->getInt32Field((JavaObject*)Meth);
  UserClass* cl = internalGetClass(vm, meth, Meth);
  JnjvmClassLoader* loader = cl->classLoader;
  return (jclass)NativeUtil::getClassType(loader, meth->getSignature()->ret);
}


JNIEXPORT jobject JNICALL Java_java_lang_reflect_Method_getParameterTypes(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
jobject Meth) {
  Jnjvm* vm = JavaThread::get()->getJVM();
  JavaField* slot = vm->upcalls->methodSlot;
  JavaMethod* meth = (JavaMethod*)slot->getInt32Field((JavaObject*)Meth);
  UserClass* cl = internalGetClass(vm, meth, Meth);
  JnjvmClassLoader* loader = cl->classLoader;
  return (jobject)(NativeUtil::getParameterTypes(loader, meth));
}

JNIEXPORT jobject JNICALL Java_java_lang_reflect_Method_invokeNative(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
jobject Meth, jobject _obj, jobject _args, jclass Cl, jint _meth) {

  Jnjvm* vm = JavaThread::get()->getJVM();
  JavaMethod* meth = (JavaMethod*)_meth;
  JavaArray* args = (JavaArray*)_args;
  sint32 nbArgs = args ? args->size : 0;
  sint32 size = meth->getSignature()->args.size();
  JavaObject* obj = (JavaObject*)_obj;

  uintptr_t buf = (uintptr_t)alloca(size * sizeof(uint64)); 
  void* _buf = (void*)buf;
  sint32 index = 0;
  if (nbArgs == size) {
    UserCommonClass* _cl = NativeUtil::resolvedImplClass(vm, Cl, false);
    UserClass* cl = (UserClass*)_cl;
    
    if (isVirtual(meth->access)) {
      verifyNull(obj);
      if (!(obj->classOf->isAssignableFrom(cl))) {
        vm->illegalArgumentExceptionForMethod(meth, cl, obj->classOf);
      }
#ifdef ISOLATE_SHARING
      if (isInterface(cl->classDef->access)) {
        cl = obj->classOf->lookupClassFromMethod(meth);
      } else {
        cl = internalGetClass(vm, meth, Meth);
      }
#endif

    } else {
      cl->initialiseClass(vm);
    }
    
    Signdef* sign = meth->getSignature();
    JavaObject** ptr = (JavaObject**)(void*)(args->elements);     
    for (std::vector<Typedef*>::iterator i = sign->args.begin(),
         e = sign->args.end(); i != e; ++i, ++index) {
      NativeUtil::decapsulePrimitive(vm, buf, ptr[index], *i);
    }
    
    JavaObject* exc = 0;

#define RUN_METH(TYPE) \
    try{ \
      if (isVirtual(meth->access)) { \
        if (isPublic(meth->access) && !isFinal(meth->access) && \
            !isFinal(meth->classDef->access)) { \
          val = meth->invoke##TYPE##VirtualBuf(vm, cl, obj, _buf); \
        } else { \
          val = meth->invoke##TYPE##SpecialBuf(vm, cl, obj, _buf); \
        } \
      } else { \
        val = meth->invoke##TYPE##StaticBuf(vm, cl, _buf); \
      } \
    }catch(...) { \
      exc = JavaThread::getJavaException(); \
      assert(exc && "no exception?"); \
      JavaThread::clearException(); \
    } \
    \
    if (exc) { \
      if (exc->classOf->isAssignableFrom(vm->upcalls->newException)) { \
        JavaThread::get()->getJVM()->invocationTargetException(exc); \
      } else { \
        JavaThread::throwException(exc); \
      } \
    } \
    
    JavaObject* res = 0;
    Typedef* retType = sign->ret;
    if (retType->isPrimitive()) {
      PrimitiveTypedef* prim = (PrimitiveTypedef*)retType;
      if (prim->isVoid()) {
        res = 0;
        uint32 val = 0;
        RUN_METH(Int);
      } else if (prim->isBool()) {
        uint32 val = 0;
        RUN_METH(Int);
        res = vm->upcalls->boolClass->doNew(vm);
        vm->upcalls->boolValue->setInt8Field(res, val);
      } else if (prim->isByte()) {
        uint32 val = 0;
        RUN_METH(Int);
        res = vm->upcalls->byteClass->doNew(vm);
        vm->upcalls->byteValue->setInt8Field(res, val);
      } else if (prim->isChar()) {
        uint32 val = 0;
        RUN_METH(Int);
        res = vm->upcalls->charClass->doNew(vm);
        vm->upcalls->charValue->setInt16Field(res, val);
      } else if (prim->isShort()) {
        uint32 val = 0;
        RUN_METH(Int);
        res = vm->upcalls->shortClass->doNew(vm);
        vm->upcalls->shortValue->setInt16Field(res, val);
      } else if (prim->isInt()) {
        uint32 val = 0;
        RUN_METH(Int);
        res = vm->upcalls->intClass->doNew(vm);
        vm->upcalls->intValue->setInt32Field(res, val);
      } else if (prim->isLong()) {
        sint64 val = 0;
        RUN_METH(Long);
        res = vm->upcalls->longClass->doNew(vm);
        vm->upcalls->longValue->setLongField(res, val);
      } else if (prim->isFloat()) {
        float val = 0;
        RUN_METH(Float);
        res = vm->upcalls->floatClass->doNew(vm);
        vm->upcalls->floatValue->setFloatField(res, val);
      } else if (prim->isDouble()) {
        double val = 0;
        RUN_METH(Double);
        res = vm->upcalls->doubleClass->doNew(vm);
        vm->upcalls->doubleValue->setDoubleField(res, val);
      }
    } else {
      JavaObject* val = 0;
      RUN_METH(JavaObject);
      res = val;
    } 
    return (jobject)res;
  }
  vm->illegalArgumentExceptionForMethod(meth, 0, 0); 
  return 0;
}

#undef RUN_METH

JNIEXPORT jobjectArray JNICALL Java_java_lang_reflect_Method_getExceptionTypes(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
jobject _meth) {
  verifyNull(_meth);
  Jnjvm* vm = JavaThread::get()->getJVM();
  JavaField* slot = vm->upcalls->methodSlot;
  JavaMethod* meth = (JavaMethod*)slot->getInt32Field((JavaObject*)_meth);
  UserClass* cl = internalGetClass(vm, meth, _meth);
  return (jobjectArray)NativeUtil::getExceptionTypes(cl, meth);
}

}
