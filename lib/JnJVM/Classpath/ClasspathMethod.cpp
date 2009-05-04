//===- ClasspathMethod.cpp ------------------------------------------------===//
//===------------- GNU classpath java/lang/reflect/Method -----------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "types.h"

#include "Classpath.h"
#include "ClasspathReflect.h"
#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaObject.h"
#include "JavaTypes.h"
#include "JavaThread.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "JnjvmClassLoader.h"

using namespace jnjvm;

extern "C" {

JNIEXPORT jint JNICALL Java_java_lang_reflect_Method_getModifiersInternal(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
JavaObjectMethod* Meth) { 
  
  jint res = 0;

  BEGIN_NATIVE_EXCEPTION(0)
  
  JavaMethod* meth = Meth->getInternalMethod(); 
  res = meth->access;

  END_NATIVE_EXCEPTION

  return res;
}

JNIEXPORT jclass JNICALL Java_java_lang_reflect_Method_getReturnType(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
JavaObjectMethod* Meth) {

  jclass res = 0;

  BEGIN_NATIVE_EXCEPTION(0)

  UserClass* cl = Meth->getClass();
  JavaMethod* meth = Meth->getInternalMethod(); 
  JnjvmClassLoader* loader = cl->classLoader;
  res = (jclass)meth->getReturnType(loader);

  END_NATIVE_EXCEPTION

  return res;
}


JNIEXPORT jobject JNICALL Java_java_lang_reflect_Method_getParameterTypes(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
JavaObjectMethod* Meth) {

  jobject res = 0;

  BEGIN_NATIVE_EXCEPTION(0)

  UserClass* cl = Meth->getClass();
  JavaMethod* meth = Meth->getInternalMethod();
  JnjvmClassLoader* loader = cl->classLoader;
  
  res = (jobject)(meth->getParameterTypes(loader));

  END_NATIVE_EXCEPTION

  return res;
}

static jobject proceed(JavaObjectMethod* Meth, jobject _obj, jobject _args, 
                       jclass Cl, jint index) __attribute__((noinline));

static jobject proceed(JavaObjectMethod* Meth, jobject _obj, jobject _args, 
                       jclass Cl, jint index) {

  JavaObject* res = 0;
  Jnjvm* vm = JavaThread::get()->getJVM();

  JavaMethod* meth = Meth->getInternalMethod();
  
  JavaArray* args = (JavaArray*)_args;
  sint32 nbArgs = args ? args->size : 0;
  Signdef* sign = meth->getSignature();
  sint32 size = sign->nbArguments;
  JavaObject* obj = (JavaObject*)_obj;

  uintptr_t buf = size ? (uintptr_t)alloca(size * sizeof(uint64)) : 0;
  

  void* _buf = (void*)buf;
  if (nbArgs == size) {
    UserCommonClass* _cl = 
      UserCommonClass::resolvedImplClass(vm, (JavaObject*)Cl, false);
    UserClass* cl = (UserClass*)_cl;
    
    if (isVirtual(meth->access)) {
      verifyNull(obj);
      UserCommonClass* objCl = obj->getClass();
      if (!(objCl->isAssignableFrom(cl))) {
        vm->illegalArgumentExceptionForMethod(meth, cl, obj->getClass());
      }

      if (isInterface(cl->access)) {
        UserClass* methodCl = 0;
        UserClass* lookup = objCl->isArray() ? objCl->super : objCl->asClass();
        meth = lookup->lookupMethod(meth->name, meth->type, false, true,
                                    &methodCl);
      }
    } else {
      cl->initialiseClass(vm);
    }
    
    JavaObject** ptr = (JavaObject**)(void*)(args->elements);
    Typedef* const* arguments = sign->getArgumentsType();
    for (sint32 i = 0; i < size; ++i) {
      ptr[i]->decapsulePrimitive(vm, buf, arguments[i]);
    }
    
    JavaObject* exc = 0;
    JavaThread* th = JavaThread::get();

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
    } catch(...) { \
      exc = th->getJavaException(); \
      if (exc->getClass()->isAssignableFrom(vm->upcalls->newException)) { \
        th->clearException(); \
        th->getJVM()->invocationTargetException(exc); \
      } else { \
        th->throwPendingException(); \
      } \
    } \
    
    Typedef* retType = sign->getReturnType();
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
  } else {
    vm->illegalArgumentExceptionForMethod(meth, 0, 0); 
  }

  return (jobject)res;
}

#undef RUN_METH

JNIEXPORT jobject JNICALL Java_java_lang_reflect_Method_invokeNative(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
JavaObjectMethod* Meth, jobject _obj, jobject _args, jclass Cl, jint index) {
  
  jobject res = 0;

  BEGIN_NATIVE_EXCEPTION(0)

  // Create a new function because we use alloca.
  res = proceed(Meth, _obj, _args, Cl, index);
  
  END_NATIVE_EXCEPTION

  return (jobject) res;
}

JNIEXPORT jobjectArray JNICALL Java_java_lang_reflect_Method_getExceptionTypes(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
JavaObjectMethod* Meth) {

  jobjectArray res = 0;

  BEGIN_NATIVE_EXCEPTION(0)

  verifyNull(Meth);
  UserClass* cl = Meth->getClass();
  JavaMethod* meth = Meth->getInternalMethod();
  JnjvmClassLoader* loader = cl->classLoader;
  res = (jobjectArray)meth->getExceptionTypes(loader);

  END_NATIVE_EXCEPTION

  return res;
}

JNIEXPORT jobject JNICALL Java_java_lang_reflect_Method_getSignature(
#ifdef NATIVE_JNI
JNIEnv *env, 
#endif
JavaObjectMethod* Meth) {

  jobject result = 0;

  BEGIN_NATIVE_EXCEPTION(0)

  verifyNull(Meth);
  JavaMethod* meth = Meth->getInternalMethod();
  Jnjvm* vm = JavaThread::get()->getJVM();
  result = (jobject)(vm->UTF8ToStr(meth->type));
  
  END_NATIVE_EXCEPTION

  return result;
}

}
