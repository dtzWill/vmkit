//===---- JavaMetaJIT.cpp - Functions for Java internal objects -----------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <cstdarg>
#include <cstring>

#include "debug.h"

#include "JavaClass.h"
#include "JavaObject.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"

using namespace jnjvm;

#define readArgs(buf, signature, ap, jni) \
  Typedef* const* arguments = signature->getArgumentsType(); \
  for (uint32 i = 0; i < signature->nbArguments; ++i) { \
    const Typedef* type = arguments[i];\
    if (type->isPrimitive()) {\
      const PrimitiveTypedef* prim = (PrimitiveTypedef*)type;\
      if (prim->isLong()) {\
        ((sint64*)buf)[0] = va_arg(ap, sint64);\
      } else if (prim->isInt()){ \
        ((sint32*)buf)[0] = va_arg(ap, sint32);\
      } else if (prim->isChar()) { \
        ((uint32*)buf)[0] = va_arg(ap, uint32);\
      } else if (prim->isShort()) { \
        ((uint32*)buf)[0] = va_arg(ap, uint32);\
      } else if (prim->isByte()) { \
        ((uint32*)buf)[0] = va_arg(ap, uint32);\
      } else if (prim->isBool()) { \
        ((uint32*)buf)[0] = va_arg(ap, uint32);\
      } else if (prim->isFloat()) {\
        ((float*)buf)[0] = (float)va_arg(ap, double);\
      } else if (prim->isDouble()) {\
        ((double*)buf)[0] = va_arg(ap, double);\
      } else {\
        fprintf(stderr, "Can't happen");\
        abort();\
      }\
    } else{\
      if (jni) { \
        JavaObject** obj = va_arg(ap, JavaObject**);\
        if (obj) {\
          ((JavaObject**)buf)[0] = *obj;\
        } else {\
          ((JavaObject**)buf)[0] = 0;\
        }\
      } else { \
        ((JavaObject**)buf)[0] = va_arg(ap, JavaObject*);\
      } \
    }\
    buf += 8; \
  }\

//===----------------------------------------------------------------------===//
// We do not need to have special care on the GC-pointers manipulated in these
// functions (this and the buffer). Once the Java function is called, they
// are never used (because the buffer is alloca'd and "this" is only used when
// calling).
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Also, virtual calls must not be used with stubs, because the thread manager
// won't understand the frame layout of a CallBuf/CallAP calling
// LLVM code (hence native code). Therefore, lookup the real JavaMethod,
// compile it, and give it to the CallBuf/CallAP.
//===----------------------------------------------------------------------===//

#if defined(DWARF_EXCEPTIONS)

#if 1//defined(__PPC__) && !defined(__MACH__)
#define INVOKE(TYPE, TYPE_NAME, FUNC_TYPE_VIRTUAL_AP, FUNC_TYPE_STATIC_AP, FUNC_TYPE_VIRTUAL_BUF, FUNC_TYPE_STATIC_BUF) \
\
TYPE JavaMethod::invoke##TYPE_NAME##VirtualAP(Jnjvm* vm, UserClass* cl, JavaObject* obj, va_list ap, bool jni) { \
  verifyNull(obj); \
  Signdef* sign = getSignature(); \
  uintptr_t buf = (uintptr_t)alloca(sign->nbArguments * sizeof(uint64)); \
  void* _buf = (void*)buf; \
  JavaMethod* meth = this; \
  if (classDef != cl) {\
    UserClass* cl2 = 0; \
    meth = cl->lookupMethodDontThrow(name, type, false, true, &cl2); \
    assert(cl2 == cl && "Different classes in invoke virtual"); \
  } \
  void* func = meth->compiledPtr(); \
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  readArgs(buf, sign, ap, jni); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, obj, _buf);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##SpecialAP(Jnjvm* vm, UserClass* cl, JavaObject* obj, va_list ap, bool jni) {\
  verifyNull(obj);\
  Signdef* sign = getSignature(); \
  uintptr_t buf = (uintptr_t)alloca(sign->nbArguments * sizeof(uint64)); \
  void* _buf = (void*)buf; \
  void* func = this->compiledPtr();\
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  readArgs(buf, sign, ap, jni); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, obj, _buf);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##StaticAP(Jnjvm* vm, UserClass* cl, va_list ap, bool jni) {\
  if (!cl->isReady()) { \
    cl->resolveClass(); \
    cl->initialiseClass(vm); \
  } \
  \
  Signdef* sign = getSignature(); \
  uintptr_t buf = (uintptr_t)alloca(sign->nbArguments * sizeof(uint64)); \
  void* _buf = (void*)buf; \
  void* func = this->compiledPtr();\
  FUNC_TYPE_STATIC_BUF call = (FUNC_TYPE_STATIC_BUF)sign->getStaticCallBuf(); \
  readArgs(buf, sign, ap, jni); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, _buf);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##VirtualBuf(Jnjvm* vm, UserClass* cl, JavaObject* obj, void* buf) {\
  verifyNull(obj);\
  Signdef* sign = getSignature(); \
  JavaMethod* meth = this; \
  if (classDef != cl) {\
    UserClass* cl2 = 0; \
    meth = cl->lookupMethodDontThrow(name, type, false, true, &cl2); \
    assert(cl2 == cl && "Different classes in invoke virtual"); \
  } \
  void* func = meth->compiledPtr(); \
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, obj, buf);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##SpecialBuf(Jnjvm* vm, UserClass* cl, JavaObject* obj, void* buf) {\
  verifyNull(obj);\
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, obj, buf);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##StaticBuf(Jnjvm* vm, UserClass* cl, void* buf) {\
  if (!cl->isReady()) { \
    cl->resolveClass(); \
    cl->initialiseClass(vm); \
  } \
  \
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_STATIC_BUF call = (FUNC_TYPE_STATIC_BUF)sign->getStaticCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, buf);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Virtual(Jnjvm* vm, UserClass* cl, JavaObject* obj, ...) { \
  va_list ap;\
  va_start(ap, obj);\
  TYPE res = invoke##TYPE_NAME##VirtualAP(vm, cl, obj, ap);\
  va_end(ap); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Special(Jnjvm* vm, UserClass* cl, JavaObject* obj, ...) {\
  va_list ap;\
  va_start(ap, obj);\
  TYPE res = invoke##TYPE_NAME##SpecialAP(vm, cl, obj, ap);\
  va_end(ap); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Static(Jnjvm* vm, UserClass* cl, ...) {\
  va_list ap;\
  va_start(ap, cl);\
  TYPE res = invoke##TYPE_NAME##StaticAP(vm, cl, ap);\
  va_end(ap); \
  return res; \
}\

#else

#define INVOKE(TYPE, TYPE_NAME, FUNC_TYPE_VIRTUAL_AP, FUNC_TYPE_STATIC_AP, FUNC_TYPE_VIRTUAL_BUF, FUNC_TYPE_STATIC_BUF) \
\
TYPE JavaMethod::invoke##TYPE_NAME##VirtualAP(Jnjvm* vm, UserClass* cl, JavaObject* obj, va_list ap) { \
  verifyNull(obj); \
  JavaMethod* meth = this; \
  if (classDef != cl) {\
    UserClass* cl2 = 0; \
    meth = cl->lookupMethodDontThrow(name, type, false, true, &cl2); \
    assert(cl2 == cl && "Different classes in invoke virtual"); \
  } \
  void* func = meth->compiledPtr(); \
  Signdef* sign = getSignature(); \
  FUNC_TYPE_VIRTUAL_AP call = (FUNC_TYPE_VIRTUAL_AP)sign->getVirtualCallAP(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, obj, ap);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##SpecialAP(Jnjvm* vm, UserClass* cl, JavaObject* obj, va_list ap) {\
  verifyNull(obj);\
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_VIRTUAL_AP call = (FUNC_TYPE_VIRTUAL_AP)sign->getVirtualCallAP(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, obj, ap);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##StaticAP(Jnjvm* vm, UserClass* cl, va_list ap) {\
  if (!cl->isReady()) { \
    cl->resolveClass(); \
    cl->initialiseClass(vm); \
  } \
  \
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_STATIC_AP call = (FUNC_TYPE_STATIC_AP)sign->getStaticCallAP(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, ap);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##VirtualBuf(Jnjvm* vm, UserClass* cl, JavaObject* obj, void* buf) {\
  verifyNull(obj);\
  JavaMethod* meth = this; \
  if (classDef != cl) {\
    UserClass* cl2 = 0; \
    meth = cl->lookupMethodDontThrow(name, type, false, true, &cl2); \
    assert(cl2 == cl && "Different classes in invoke virtual"); \
  } \
  void* func = meth->compiledPtr(); \
  Signdef* sign = getSignature(); \
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, obj, buf);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##SpecialBuf(Jnjvm* vm, UserClass* cl, JavaObject* obj, void* buf) {\
  verifyNull(obj);\
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, obj, buf);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##StaticBuf(Jnjvm* vm, UserClass* cl, void* buf) {\
  if (!cl->isReady()) { \
    cl->resolveClass(); \
    cl->initialiseClass(vm); \
  } \
  \
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_STATIC_BUF call = (FUNC_TYPE_STATIC_BUF)sign->getStaticCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  try { \
    res = call(cl->getConstantPool(), func, buf);\
  } catch (...) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Virtual(Jnjvm* vm, UserClass* cl, JavaObject* obj, ...) { \
  va_list ap;\
  va_start(ap, obj);\
  TYPE res = invoke##TYPE_NAME##VirtualAP(vm, cl, obj, ap);\
  va_end(ap); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Special(Jnjvm* vm, UserClass* cl, JavaObject* obj, ...) {\
  va_list ap;\
  va_start(ap, obj);\
  TYPE res = invoke##TYPE_NAME##SpecialAP(vm, cl, obj, ap);\
  va_end(ap); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Static(Jnjvm* vm, UserClass* cl, ...) {\
  va_list ap;\
  va_start(ap, cl);\
  TYPE res = invoke##TYPE_NAME##StaticAP(vm, cl, ap);\
  va_end(ap); \
  return res; \
}\

#endif

#else // DWARF_EXCEPTIONS

#if 1//defined(__PPC__) && !defined(__MACH__)
#define INVOKE(TYPE, TYPE_NAME, FUNC_TYPE_VIRTUAL_AP, FUNC_TYPE_STATIC_AP, FUNC_TYPE_VIRTUAL_BUF, FUNC_TYPE_STATIC_BUF) \
\
TYPE JavaMethod::invoke##TYPE_NAME##VirtualAP(Jnjvm* vm, UserClass* cl, JavaObject* obj, va_list ap, bool jni) { \
  verifyNull(obj); \
  Signdef* sign = getSignature(); \
  uintptr_t buf = (uintptr_t)alloca(sign->nbArguments * sizeof(uint64)); \
  void* _buf = (void*)buf; \
  JavaMethod* meth = this; \
  if (classDef != cl) {\
    UserClass* cl2 = 0; \
    meth = cl->lookupMethodDontThrow(name, type, false, true, &cl2); \
    assert(cl2 == cl && "Different classes in invoke virtual"); \
  } \
  void* func = meth->compiledPtr(); \
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  readArgs(buf, sign, ap, jni); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, obj, _buf);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##SpecialAP(Jnjvm* vm, UserClass* cl, JavaObject* obj, va_list ap, bool jni) {\
  verifyNull(obj);\
  Signdef* sign = getSignature(); \
  uintptr_t buf = (uintptr_t)alloca(sign->nbArguments * sizeof(uint64)); \
  void* _buf = (void*)buf; \
  void* func = this->compiledPtr();\
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  readArgs(buf, sign, ap, jni); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, obj, _buf);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##StaticAP(Jnjvm* vm, UserClass* cl, va_list ap, bool jni) {\
  if (!cl->isReady()) { \
    cl->resolveClass(); \
    cl->initialiseClass(vm); \
  } \
  \
  Signdef* sign = getSignature(); \
  uintptr_t buf = (uintptr_t)alloca(sign->nbArguments * sizeof(uint64)); \
  void* _buf = (void*)buf; \
  void* func = this->compiledPtr();\
  FUNC_TYPE_STATIC_BUF call = (FUNC_TYPE_STATIC_BUF)sign->getStaticCallBuf(); \
  readArgs(buf, sign, ap, jni); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, _buf);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##VirtualBuf(Jnjvm* vm, UserClass* cl, JavaObject* obj, void* buf) {\
  verifyNull(obj);\
  Signdef* sign = getSignature(); \
  JavaMethod* meth = this; \
  if (classDef != cl) {\
    UserClass* cl2 = 0; \
    meth = cl->lookupMethodDontThrow(name, type, false, true, &cl2); \
    assert(cl2 == cl && "Different classes in invoke virtual"); \
  } \
  void* func = meth->compiledPtr(); \
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, obj, buf);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##SpecialBuf(Jnjvm* vm, UserClass* cl, JavaObject* obj, void* buf) {\
  verifyNull(obj);\
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, obj, buf);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##StaticBuf(Jnjvm* vm, UserClass* cl, void* buf) {\
  if (!cl->isReady()) { \
    cl->resolveClass(); \
    cl->initialiseClass(vm); \
  } \
  \
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_STATIC_BUF call = (FUNC_TYPE_STATIC_BUF)sign->getVirtualCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, buf);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Virtual(Jnjvm* vm, UserClass* cl, JavaObject* obj, ...) { \
  va_list ap;\
  va_start(ap, obj);\
  TYPE res = invoke##TYPE_NAME##VirtualAP(vm, cl, obj, ap);\
  va_end(ap); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Special(Jnjvm* vm, UserClass* cl, JavaObject* obj, ...) {\
  va_list ap;\
  va_start(ap, obj);\
  TYPE res = invoke##TYPE_NAME##SpecialAP(vm, cl, obj, ap);\
  va_end(ap); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Static(Jnjvm* vm, UserClass* cl, ...) {\
  va_list ap;\
  va_start(ap, cl);\
  TYPE res = invoke##TYPE_NAME##StaticAP(vm, cl, ap);\
  va_end(ap); \
  return res; \
}\

#else

#define INVOKE(TYPE, TYPE_NAME, FUNC_TYPE_VIRTUAL_AP, FUNC_TYPE_STATIC_AP, FUNC_TYPE_VIRTUAL_BUF, FUNC_TYPE_STATIC_BUF) \
\
TYPE JavaMethod::invoke##TYPE_NAME##VirtualAP(Jnjvm* vm, UserClass* cl, JavaObject* obj, va_list ap) { \
  verifyNull(obj); \
  JavaMethod* meth = this; \
  if (classDef != cl) {\
    UserClass* cl2 = 0; \
    meth = cl->lookupMethodDontThrow(name, type, false, true, &cl2); \
    assert(cl2 == cl && "Different classes in invoke virtual"); \
  } \
  void* func = meth->compiledPtr(); \
  Signdef* sign = getSignature(); \
  FUNC_TYPE_VIRTUAL_AP call = (FUNC_TYPE_VIRTUAL_AP)sign->getVirtualCallAP(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, obj, ap);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##SpecialAP(Jnjvm* vm, UserClass* cl, JavaObject* obj, va_list ap) {\
  verifyNull(obj);\
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_VIRTUAL_AP call = (FUNC_TYPE_VIRTUAL_AP)sign->getVirtualCallAP(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, obj, ap);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##StaticAP(Jnjvm* vm, UserClass* cl, va_list ap) {\
  if (!cl->isReady()) { \
    cl->resolveClass(); \
    cl->initialiseClass(vm); \
  } \
  \
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_STATIC_AP call = (FUNC_TYPE_STATIC_AP)sign->getStaticCallAP(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, ap);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##VirtualBuf(Jnjvm* vm, UserClass* cl, JavaObject* obj, void* buf) {\
  verifyNull(obj);\
  JavaMethod* meth = this; \
  if (classDef != cl) {\
    UserClass* cl2 = 0; \
    meth = cl->lookupMethodDontThrow(name, type, false, true, &cl2); \
    assert(cl2 == cl && "Different classes in invoke virtual"); \
  } \
  void* func = meth->compiledPtr(); \
  Signdef* sign = getSignature(); \
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, obj, buf);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##SpecialBuf(Jnjvm* vm, UserClass* cl, JavaObject* obj, void* buf) {\
  verifyNull(obj);\
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_VIRTUAL_BUF call = (FUNC_TYPE_VIRTUAL_BUF)sign->getVirtualCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, obj, buf);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##StaticBuf(Jnjvm* vm, UserClass* cl, void* buf) {\
  if (!cl->isReady()) { \
    cl->resolveClass(); \
    cl->initialiseClass(vm); \
  } \
  \
  void* func = this->compiledPtr();\
  Signdef* sign = getSignature(); \
  FUNC_TYPE_STATIC_BUF call = (FUNC_TYPE_STATIC_BUF)sign->getStaticCallBuf(); \
  JavaThread* th = JavaThread::get(); \
  th->startJava(); \
  TYPE res = 0; \
  res = call(cl->getConstantPool(), func, buf);\
  if (th->pendingException) { \
    th->throwFromJava(); \
  } \
  th->endJava(); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Virtual(Jnjvm* vm, UserClass* cl, JavaObject* obj, ...) { \
  va_list ap;\
  va_start(ap, obj);\
  TYPE res = invoke##TYPE_NAME##VirtualAP(vm, cl, obj, ap);\
  va_end(ap); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Special(Jnjvm* vm, UserClass* cl, JavaObject* obj, ...) {\
  va_list ap;\
  va_start(ap, obj);\
  TYPE res = invoke##TYPE_NAME##SpecialAP(vm, cl, obj, ap);\
  va_end(ap); \
  return res; \
}\
\
TYPE JavaMethod::invoke##TYPE_NAME##Static(Jnjvm* vm, UserClass* cl, ...) {\
  va_list ap;\
  va_start(ap, cl);\
  TYPE res = invoke##TYPE_NAME##StaticAP(vm, cl, ap);\
  va_end(ap); \
  return res; \
}\

#endif
#endif

typedef uint32 (*uint32_virtual_ap)(UserConstantPool*, void*, JavaObject*, va_list);
typedef sint64 (*sint64_virtual_ap)(UserConstantPool*, void*, JavaObject*, va_list);
typedef float  (*float_virtual_ap)(UserConstantPool*, void*, JavaObject*, va_list);
typedef double (*double_virtual_ap)(UserConstantPool*, void*, JavaObject*, va_list);
typedef JavaObject* (*object_virtual_ap)(UserConstantPool*, void*, JavaObject*, va_list);

typedef uint32 (*uint32_static_ap)(UserConstantPool*, void*, va_list);
typedef sint64 (*sint64_static_ap)(UserConstantPool*, void*, va_list);
typedef float  (*float_static_ap)(UserConstantPool*, void*, va_list);
typedef double (*double_static_ap)(UserConstantPool*, void*, va_list);
typedef JavaObject* (*object_static_ap)(UserConstantPool*, void*, va_list);

typedef uint32 (*uint32_virtual_buf)(UserConstantPool*, void*, JavaObject*, void*);
typedef sint64 (*sint64_virtual_buf)(UserConstantPool*, void*, JavaObject*, void*);
typedef float  (*float_virtual_buf)(UserConstantPool*, void*, JavaObject*, void*);
typedef double (*double_virtual_buf)(UserConstantPool*, void*, JavaObject*, void*);
typedef JavaObject* (*object_virtual_buf)(UserConstantPool*, void*, JavaObject*, void*);

typedef uint32 (*uint32_static_buf)(UserConstantPool*, void*, void*);
typedef sint64 (*sint64_static_buf)(UserConstantPool*, void*, void*);
typedef float  (*float_static_buf)(UserConstantPool*, void*, void*);
typedef double (*double_static_buf)(UserConstantPool*, void*, void*);
typedef JavaObject* (*object_static_buf)(UserConstantPool*, void*, void*);

INVOKE(uint32, Int, uint32_virtual_ap, uint32_static_ap, uint32_virtual_buf, uint32_static_buf)
INVOKE(sint64, Long, sint64_virtual_ap, sint64_static_ap, sint64_virtual_buf, sint64_static_buf)
INVOKE(float,  Float, float_virtual_ap,  float_static_ap,  float_virtual_buf,  float_static_buf)
INVOKE(double, Double, double_virtual_ap, double_static_ap, double_virtual_buf, double_static_buf)
INVOKE(JavaObject*, JavaObject, object_virtual_ap, object_static_ap, object_virtual_buf, object_static_buf)

#undef INVOKE

void Jnjvm::invokeFinalizer(gc* _obj) {
  JavaObject* obj = (JavaObject*)_obj;
  llvm_gcroot(obj, 0);
  JavaMethod* meth = upcalls->FinalizeObject;
  UserClass* cl = obj->getClass()->asClass();
  meth->invokeIntVirtualBuf(this, cl, obj, 0);
}

bool Jnjvm::enqueueReference(gc* _obj) {
  JavaObject* obj = (JavaObject*)_obj;
  llvm_gcroot(obj, 0);
  JavaMethod* meth = upcalls->EnqueueReference;
  UserClass* cl = obj->getClass()->asClass();
  return (bool)meth->invokeIntSpecialBuf(this, cl, obj, 0);
}
