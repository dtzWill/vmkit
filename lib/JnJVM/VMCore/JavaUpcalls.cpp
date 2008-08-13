//===-------- JavaUpcalls.cpp - Upcalls to Java entities ------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <vector>

#include "mvm/JIT.h"

#include "JavaAccess.h"
#include "JavaClass.h"
#include "JavaJIT.h"
#include "JavaObject.h"
#include "JavaString.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "JnjvmModule.h"

#define COMPILE_METHODS(cl) \
  for (CommonClass::method_iterator i = cl->virtualMethods.begin(), \
            e = cl->virtualMethods.end(); i!= e; ++i) { \
    i->second->compiledPtr(); \
  } \
  \
  for (CommonClass::method_iterator i = cl->staticMethods.begin(), \
            e = cl->staticMethods.end(); i!= e; ++i) { \
    i->second->compiledPtr(); \
  }


using namespace jnjvm;

Class*      ClasspathThread::newThread;
Class*      ClasspathThread::newVMThread;
JavaField*  ClasspathThread::assocThread;
JavaField*  ClasspathThread::vmdata;
JavaMethod* ClasspathThread::finaliseCreateInitialThread;
JavaMethod* ClasspathThread::initVMThread;
JavaMethod* ClasspathThread::groupAddThread;
JavaField*  ClasspathThread::name;
JavaField*  ClasspathThread::priority;
JavaField*  ClasspathThread::daemon;
JavaField*  ClasspathThread::group;
JavaField*  ClasspathThread::running;
JavaField*  ClasspathThread::rootGroup;
JavaField*  ClasspathThread::vmThread;
JavaMethod* ClasspathThread::uncaughtException;

JavaMethod* Classpath::setContextClassLoader;
JavaMethod* Classpath::getSystemClassLoader;
Class*      Classpath::newString;
Class*      Classpath::newClass;
Class*      Classpath::newThrowable;
Class*      Classpath::newException;
JavaMethod* Classpath::initClass;
JavaMethod* Classpath::initClassWithProtectionDomain;
JavaField*  Classpath::vmdataClass;
JavaMethod* Classpath::setProperty;
JavaMethod* Classpath::initString;
JavaMethod* Classpath::getCallingClassLoader;
JavaMethod* Classpath::initConstructor;
Class*      Classpath::newConstructor;
ClassArray* Classpath::constructorArrayClass;
ClassArray* Classpath::constructorArrayAnnotation;
JavaField*  Classpath::constructorSlot;
JavaMethod* Classpath::initMethod;
JavaMethod* Classpath::initField;
Class*      Classpath::newField;
Class*      Classpath::newMethod;
ClassArray* Classpath::methodArrayClass;
ClassArray* Classpath::fieldArrayClass;
JavaField*  Classpath::methodSlot;
JavaField*  Classpath::fieldSlot;
ClassArray* Classpath::classArrayClass;
JavaMethod* Classpath::loadInClassLoader;
JavaMethod* Classpath::initVMThrowable;
JavaField*  Classpath::vmDataVMThrowable;
Class*      Classpath::newVMThrowable;
JavaField*  Classpath::bufferAddress;
JavaField*  Classpath::dataPointer32;
JavaField*  Classpath::vmdataClassLoader;

JavaField*  Classpath::boolValue;
JavaField*  Classpath::byteValue;
JavaField*  Classpath::shortValue;
JavaField*  Classpath::charValue;
JavaField*  Classpath::intValue;
JavaField*  Classpath::longValue;
JavaField*  Classpath::floatValue;
JavaField*  Classpath::doubleValue;

Class*      Classpath::newStackTraceElement;
ClassArray* Classpath::stackTraceArray;
JavaMethod* Classpath::initStackTraceElement;

Class* Classpath::voidClass;
Class* Classpath::boolClass;
Class* Classpath::byteClass;
Class* Classpath::shortClass;
Class* Classpath::charClass;
Class* Classpath::intClass;
Class* Classpath::floatClass;
Class* Classpath::doubleClass;
Class* Classpath::longClass;

Class* Classpath::vmStackWalker;

void ClasspathThread::initialise(Jnjvm* vm) {
  newThread = 
    UPCALL_CLASS(vm, "java/lang/Thread");
  
  newVMThread = 
    UPCALL_CLASS(vm, "java/lang/VMThread");
  
  assocThread = 
    UPCALL_FIELD(vm, "java/lang/VMThread", "thread", "Ljava/lang/Thread;",
                 ACC_VIRTUAL);
  
  vmdata = 
    UPCALL_FIELD(vm, "java/lang/VMThread", "vmdata", "Ljava/lang/Object;",
                 ACC_VIRTUAL);
  
  finaliseCreateInitialThread = 
    UPCALL_METHOD(vm, "java/lang/InheritableThreadLocal", "newChildThread",
                  "(Ljava/lang/Thread;)V", ACC_STATIC);
  
  initVMThread = 
    UPCALL_METHOD(vm, "java/lang/VMThread", "<init>",
                  "(Ljava/lang/Thread;)V", ACC_VIRTUAL);

  groupAddThread = 
    UPCALL_METHOD(vm, "java/lang/ThreadGroup", "addThread",
                  "(Ljava/lang/Thread;)V", ACC_VIRTUAL);
  
  name = 
    UPCALL_FIELD(vm, "java/lang/Thread", "name", "Ljava/lang/String;",
                 ACC_VIRTUAL);
  
  priority = 
    UPCALL_FIELD(vm,  "java/lang/Thread", "priority", "I", ACC_VIRTUAL);

  daemon = 
    UPCALL_FIELD(vm, "java/lang/Thread", "daemon", "Z", ACC_VIRTUAL);

  group =
    UPCALL_FIELD(vm, "java/lang/Thread", "group",
                 "Ljava/lang/ThreadGroup;", ACC_VIRTUAL);
  
  running = 
    UPCALL_FIELD(vm, "java/lang/VMThread", "running", "Z", ACC_VIRTUAL);
  
  rootGroup =
    UPCALL_FIELD(vm, "java/lang/ThreadGroup", "root",
                 "Ljava/lang/ThreadGroup;", ACC_STATIC);

  vmThread = 
    UPCALL_FIELD(vm, "java/lang/Thread", "vmThread",
                 "Ljava/lang/VMThread;", ACC_VIRTUAL);
  
  uncaughtException = 
    UPCALL_METHOD(vm, "java/lang/ThreadGroup",  "uncaughtException",
                  "(Ljava/lang/Thread;Ljava/lang/Throwable;)V", ACC_VIRTUAL);
}

void ClasspathThread::createInitialThread(Jnjvm* vm, JavaObject* th) {
  vm->loadName(newVMThread->name, newVMThread->classLoader, true, true, true);

  JavaObject* vmth = newVMThread->doNew(vm);
  name->setVirtualObjectField(th, (JavaObject*)vm->asciizToStr("main"));
  priority->setVirtualInt32Field(th, (uint32)1);
  daemon->setVirtualInt8Field(th, (uint32)0);
  vmThread->setVirtualObjectField(th, vmth);
  assocThread->setVirtualObjectField(vmth, th);
  running->setVirtualInt8Field(vmth, (uint32)1);
  
  rootGroup->classDef->isolate->loadName(rootGroup->classDef->name,
                                         rootGroup->classDef->classLoader,
                                         true, true, true);
  JavaObject* RG = rootGroup->getStaticObjectField();
  group->setVirtualObjectField(th, RG);
  groupAddThread->invokeIntSpecial(vm, RG, th);
}

void ClasspathThread::mapInitialThread(Jnjvm* vm) {
  vm->loadName(newThread->name, newThread->classLoader, true, true, true);
  JavaObject* th = newThread->doNew(vm);
  createInitialThread(vm, th);
  JavaThread* myth = JavaThread::get();
  myth->javaThread = th;
  JavaObject* vmth = vmThread->getVirtualObjectField(th);
  vmdata->setVirtualObjectField(vmth, (JavaObject*)myth);
  finaliseCreateInitialThread->invokeIntStatic(vm, th);
}

void Classpath::initialiseClasspath(Jnjvm* vm) {
  getSystemClassLoader =
    UPCALL_METHOD(vm, "java/lang/ClassLoader", "getSystemClassLoader",
                  "()Ljava/lang/ClassLoader;", ACC_STATIC);

  setContextClassLoader =
    UPCALL_METHOD(vm, "java/lang/Thread", "setContextClassLoader",
                  "(Ljava/lang/ClassLoader;)V", ACC_VIRTUAL);

  newString = 
    UPCALL_CLASS(vm, "java/lang/String");
  
  newClass =
    UPCALL_CLASS(vm, "java/lang/Class");
  
  newThrowable =
    UPCALL_CLASS(vm, "java/lang/Throwable");
  
  newException =
    UPCALL_CLASS(vm, "java/lang/Exception");
  
  initClass =
    UPCALL_METHOD(vm, "java/lang/Class", "<init>", "(Ljava/lang/Object;)V",
                  ACC_VIRTUAL);

  initClassWithProtectionDomain =
    UPCALL_METHOD(vm, "java/lang/Class", "<init>",
                  "(Ljava/lang/Object;Ljava/security/ProtectionDomain;)V",
                  ACC_VIRTUAL);

  vmdataClass =
    UPCALL_FIELD(vm, "java/lang/Class", "vmdata", "Ljava/lang/Object;",
                 ACC_VIRTUAL);
  
  setProperty = 
    UPCALL_METHOD(vm, "java/util/Properties", "setProperty",
                  "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Object;",
                  ACC_VIRTUAL);

  initString =
    UPCALL_METHOD(vm, "java/lang/String", "<init>", "([CIIZ)V", ACC_VIRTUAL);
  
  initConstructor =
    UPCALL_METHOD(vm, "java/lang/reflect/Constructor", "<init>",
                  "(Ljava/lang/Class;I)V", ACC_VIRTUAL);

  newConstructor =
    UPCALL_CLASS(vm, "java/lang/reflect/Constructor");

  constructorArrayClass =
    UPCALL_ARRAY_CLASS(vm, "java/lang/reflect/Constructor", 1);
  
  constructorArrayAnnotation =
    UPCALL_ARRAY_CLASS(vm, "java/lang/annotation/Annotation", 1);

  constructorSlot =
    UPCALL_FIELD(vm, "java/lang/reflect/Constructor", "slot", "I", ACC_VIRTUAL);
  
  initMethod =
    UPCALL_METHOD(vm, "java/lang/reflect/Method", "<init>",
                  "(Ljava/lang/Class;Ljava/lang/String;I)V", ACC_VIRTUAL);

  newMethod =
    UPCALL_CLASS(vm, "java/lang/reflect/Method");

  methodArrayClass =
    UPCALL_ARRAY_CLASS(vm, "java/lang/reflect/Method", 1);

  methodSlot =
    UPCALL_FIELD(vm, "java/lang/reflect/Method", "slot", "I", ACC_VIRTUAL);
  
  initField =
    UPCALL_METHOD(vm, "java/lang/reflect/Field", "<init>",
                  "(Ljava/lang/Class;Ljava/lang/String;I)V", ACC_VIRTUAL);

  newField =
    UPCALL_CLASS(vm, "java/lang/reflect/Field");

  fieldArrayClass =
    UPCALL_ARRAY_CLASS(vm, "java/lang/reflect/Field", 1);
  
  fieldSlot =
    UPCALL_FIELD(vm, "java/lang/reflect/Field", "slot", "I", ACC_VIRTUAL);
  
  
  classArrayClass =
    UPCALL_ARRAY_CLASS(vm, "java/lang/Class", 1);
  
  newVMThrowable =
    UPCALL_CLASS(vm, "java/lang/VMThrowable");
  
  initVMThrowable =
    UPCALL_METHOD(vm, "java/lang/VMThrowable", "<init>", "()V", ACC_VIRTUAL);

  vmDataVMThrowable =
    UPCALL_FIELD(vm, "java/lang/VMThrowable", "vmdata", "Ljava/lang/Object;",
                 ACC_VIRTUAL);

  bufferAddress =
    UPCALL_FIELD(vm, "java/nio/Buffer", "address", "Lgnu/classpath/Pointer;",
                 ACC_VIRTUAL);

  dataPointer32 =
    UPCALL_FIELD(vm, "gnu/classpath/Pointer32", "data", "I", ACC_VIRTUAL);

  vmdataClassLoader =
    UPCALL_FIELD(vm, "java/lang/ClassLoader", "vmdata", "Ljava/lang/Object;",
                 ACC_VIRTUAL);
  
  newStackTraceElement =
    UPCALL_CLASS(vm, "java/lang/StackTraceElement");
  
  stackTraceArray =
    UPCALL_ARRAY_CLASS(vm, "java/lang/StackTraceElement", 1);

  initStackTraceElement =
    UPCALL_METHOD(vm,  "java/lang/StackTraceElement", "<init>",
                  "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Z)V",
                  ACC_VIRTUAL);

  boolValue =
    UPCALL_FIELD(vm, "java/lang/Boolean", "value", "Z", ACC_VIRTUAL);
  
  byteValue =
    UPCALL_FIELD(vm, "java/lang/Byte", "value", "B", ACC_VIRTUAL);

  shortValue =
    UPCALL_FIELD(vm, "java/lang/Short", "value", "S", ACC_VIRTUAL);

  charValue =
    UPCALL_FIELD(vm, "java/lang/Character", "value", "C", ACC_VIRTUAL);

  intValue =
    UPCALL_FIELD(vm, "java/lang/Integer", "value", "I", ACC_VIRTUAL);

  longValue =
    UPCALL_FIELD(vm, "java/lang/Long", "value", "J", ACC_VIRTUAL);

  floatValue =
    UPCALL_FIELD(vm, "java/lang/Float", "value", "F", ACC_VIRTUAL);

  doubleValue =
    UPCALL_FIELD(vm, "java/lang/Double", "value", "D", ACC_VIRTUAL);

  Classpath::voidClass =
    UPCALL_CLASS(vm, "java/lang/Void");
  
  Classpath::boolClass =
    UPCALL_CLASS(vm, "java/lang/Boolean");

  Classpath::byteClass =
    UPCALL_CLASS(vm, "java/lang/Byte");

  Classpath::shortClass =
    UPCALL_CLASS(vm, "java/lang/Short");

  Classpath::charClass =
    UPCALL_CLASS(vm, "java/lang/Character"); 

  Classpath::intClass =
    UPCALL_CLASS(vm, "java/lang/Integer");

  Classpath::floatClass =
    UPCALL_CLASS(vm, "java/lang/Float");

  Classpath::doubleClass =
    UPCALL_CLASS(vm, "java/lang/Double");

  Classpath::longClass =
    UPCALL_CLASS(vm, "java/lang/Long");

  vmStackWalker =
    UPCALL_CLASS(vm, "gnu/classpath/VMStackWalker");

  loadInClassLoader =
    UPCALL_METHOD(vm, "java/lang/ClassLoader", "loadClass",
                  "(Ljava/lang/String;Z)Ljava/lang/Class;", ACC_VIRTUAL);

  JavaMethod* internString =
    UPCALL_METHOD(vm, "java/lang/VMString", "intern",
                  "(Ljava/lang/String;)Ljava/lang/String;", ACC_STATIC); 
  vm->TheModule->setMethod(internString, "internString");
  
  JavaMethod* isArray =
    UPCALL_METHOD(vm, "java/lang/Class", "isArray", "()Z", ACC_VIRTUAL);
  vm->TheModule->setMethod(isArray, "isArray");

  ClasspathThread::initialise(vm);
    
  vm->loadName(vm->asciizConstructUTF8("java/lang/String"), 
                                       CommonClass::jnjvmClassLoader, true,
                                       false, false);

  CommonClass* object = 
    vm->loadName(vm->asciizConstructUTF8("java/lang/Object"), 
                                         CommonClass::jnjvmClassLoader, true,
                                         false, false);
  COMPILE_METHODS(object)
  
  JavaMethod* getCallingClass =
    UPCALL_METHOD(vm, "gnu/classpath/VMStackWalker", "getCallingClass",
                  "()Ljava/lang/Class;", ACC_STATIC);
  vm->TheModule->setMethod(getCallingClass, "getCallingClass");
  
  JavaMethod* getCallingClassLoader =
    UPCALL_METHOD(vm, "gnu/classpath/VMStackWalker", "getCallingClassLoader",
                  "()Ljava/lang/ClassLoader;", ACC_STATIC);
  vm->TheModule->setMethod(getCallingClassLoader, "getCallingClassLoader");
  
  JavaMethod* postProperties =
    UPCALL_METHOD(vm, "gnu/classpath/VMSystemProperties", "postInit",
                  "(Ljava/util/Properties;)V", ACC_STATIC);
  vm->TheModule->setMethod(postProperties, "propertiesPostInit");
}

extern "C" JavaString* internString(JavaString* obj) {
  Jnjvm* vm = JavaThread::get()->isolate;
  const UTF8* utf8 = obj->strToUTF8(vm);
  return vm->UTF8ToStr(utf8);
}

extern "C" uint8 isArray(JavaObject* klass) {
  CommonClass* cl = 
    (CommonClass*)((Classpath::vmdataClass->getVirtualObjectField(klass)));

  return (uint8)cl->isArray;
}
