//===---------- Jnjvm.cpp - Java virtual machine description --------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "debug.h"

#include "mvm/JIT.h"

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaConstantPool.h"
#include "JavaJIT.h"
#include "JavaString.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "JnjvmModuleProvider.h"
#include "LockedMap.h"
#include "Reader.h"
#ifdef SERVICE_VM
#include "ServiceDomain.h"
#endif
#include "Zip.h"

using namespace jnjvm;

Jnjvm* Jnjvm::bootstrapVM = 0;

#define DEF_UTF8(var) \
  const UTF8* Jnjvm::var = 0;
  
  DEF_UTF8(initName);
  DEF_UTF8(clinitName);
  DEF_UTF8(clinitType);
  DEF_UTF8(runName);
  DEF_UTF8(prelib);
  DEF_UTF8(postlib);
  DEF_UTF8(mathName);
  DEF_UTF8(abs);
  DEF_UTF8(sqrt);
  DEF_UTF8(sin);
  DEF_UTF8(cos);
  DEF_UTF8(tan);
  DEF_UTF8(asin);
  DEF_UTF8(acos);
  DEF_UTF8(atan);
  DEF_UTF8(atan2);
  DEF_UTF8(exp);
  DEF_UTF8(log);
  DEF_UTF8(pow);
  DEF_UTF8(ceil);
  DEF_UTF8(floor);
  DEF_UTF8(rint);
  DEF_UTF8(cbrt);
  DEF_UTF8(cosh);
  DEF_UTF8(expm1);
  DEF_UTF8(hypot);
  DEF_UTF8(log10);
  DEF_UTF8(log1p);
  DEF_UTF8(sinh);
  DEF_UTF8(tanh);

#undef DEF_UTF8

const char* Jnjvm::dirSeparator = "/";
const char* Jnjvm::envSeparator = ":";
const unsigned int Jnjvm::Magic = 0xcafebabe;

#define DECLARE_EXCEPTION(EXCP) \
  const char* Jnjvm::EXCP = "java/lang/"#EXCP

#define DECLARE_REFLECT_EXCEPTION(EXCP) \
  const char* Jnjvm::EXCP = "java/lang/reflect/"#EXCP

DECLARE_EXCEPTION(ArithmeticException);
DECLARE_REFLECT_EXCEPTION(InvocationTargetException);
DECLARE_EXCEPTION(ArrayStoreException);
DECLARE_EXCEPTION(ClassCastException);
DECLARE_EXCEPTION(IllegalMonitorStateException);
DECLARE_EXCEPTION(IllegalArgumentException);
DECLARE_EXCEPTION(InterruptedException);
DECLARE_EXCEPTION(IndexOutOfBoundsException);
DECLARE_EXCEPTION(ArrayIndexOutOfBoundsException);
DECLARE_EXCEPTION(NegativeArraySizeException);
DECLARE_EXCEPTION(NullPointerException);
DECLARE_EXCEPTION(SecurityException);
DECLARE_EXCEPTION(ClassFormatError);
DECLARE_EXCEPTION(ClassCircularityError);
DECLARE_EXCEPTION(NoClassDefFoundError);
DECLARE_EXCEPTION(UnsupportedClassVersionError);
DECLARE_EXCEPTION(NoSuchFieldError);
DECLARE_EXCEPTION(NoSuchMethodError);
DECLARE_EXCEPTION(InstantiationError);
DECLARE_EXCEPTION(IllegalAccessError);
DECLARE_EXCEPTION(IllegalAccessException);
DECLARE_EXCEPTION(VerifyError);
DECLARE_EXCEPTION(ExceptionInInitializerError);
DECLARE_EXCEPTION(LinkageError);
DECLARE_EXCEPTION(AbstractMethodError);
DECLARE_EXCEPTION(UnsatisfiedLinkError);
DECLARE_EXCEPTION(InternalError);
DECLARE_EXCEPTION(OutOfMemoryError);
DECLARE_EXCEPTION(StackOverflowError);
DECLARE_EXCEPTION(UnknownError);
DECLARE_EXCEPTION(ClassNotFoundException);


void Jnjvm::analyseClasspathEnv(const char* str) {
  if (str != 0) {
    unsigned int len = strlen(str);
    char* buf = (char*)alloca(len + 1);
    const char* cur = str;
    int top = 0;
    char c = 1;
    while (c != 0) {
      while (((c = cur[top]) != 0) && c != envSeparator[0]) {
        top++;
      }
      if (top != 0) {
        memcpy(buf, cur, top);
        buf[top] = 0;
        char* rp = (char*)malloc(4096);
        memset(rp, 0, 4096);
        rp = realpath(buf, rp);
        if (rp[4095] == 0 && strlen(rp) != 0) {
          struct stat st;
          stat(rp, &st);
          if ((st.st_mode & S_IFMT) == S_IFDIR) {
            unsigned int len = strlen(rp);
            char* temp = (char*)malloc(len + 2);
            memcpy(temp, rp, len);
            temp[len] = dirSeparator[0];
            temp[len + 1] = 0;
            bootClasspath.push_back(temp);
            free(rp);
          } else {
            bootClasspath.push_back(rp);
          }
        } else {
          free(rp);
        }
      }
      cur = cur + top + 1;
      top = 0;
    }
  }
}

void Jnjvm::readParents(Class* cl, Reader* reader) {
  JavaCtpInfo* ctpInfo = cl->ctpInfo;
  unsigned short int superEntry = reader->readU2();
  const UTF8* super = superEntry ? 
        ctpInfo->resolveClassName(superEntry) : 0;

  unsigned short int nbI = reader->readU2();
  cl->superUTF8 = super;
  for (int i = 0; i < nbI; i++)
    cl->interfacesUTF8.push_back(ctpInfo->resolveClassName(reader->readU2()));

}

void Jnjvm::loadParents(Class* cl) {
  const UTF8* super = cl->superUTF8;
  int nbI = cl->interfacesUTF8.size();
  JavaObject* classLoader = cl->classLoader;
  if (super == 0) {
    cl->depth = 0;
    cl->display.push_back(cl);
    cl->virtualTableSize = VT_SIZE / sizeof(void*);
  } else {
    cl->super = loadName(super, classLoader, true, false, true);
    int depth = cl->super->depth;
    cl->depth = depth + 1;
    cl->virtualTableSize = cl->super->virtualTableSize;
    for (uint32 i = 0; i < cl->super->display.size(); ++i) {
      cl->display.push_back(cl->super->display[i]);
    }
    cl->display.push_back(cl);
  }

  for (int i = 0; i < nbI; i++)
    cl->interfaces.push_back((Class*)loadName(cl->interfacesUTF8[i],
                                              classLoader, true, false, true));
}

void Jnjvm::readAttributs(Class* cl, Reader* reader,
                           std::vector<Attribut*,
                                       gc_allocator<Attribut*> >& attr) {
  JavaCtpInfo* ctpInfo = cl->ctpInfo;
  unsigned short int nba = reader->readU2();
  
  for (int i = 0; i < nba; i++) {
    const UTF8* attName = ctpInfo->UTF8At(reader->readU2());
    unsigned int attLen = reader->readU4();
    Attribut* att = vm_new(this, Attribut);
    att->derive(attName, attLen, reader);
    attr.push_back(att);
    reader->seek(attLen, Reader::SeekCur);
  }
}

void Jnjvm::readFields(Class* cl, Reader* reader) {
  unsigned short int nbFields = reader->readU2();
  JavaCtpInfo* ctpInfo = cl->ctpInfo;
  for (int i = 0; i < nbFields; i++) {
    unsigned short int access = reader->readU2();
    const UTF8* name = ctpInfo->UTF8At(reader->readU2());
    const UTF8* type = ctpInfo->UTF8At(reader->readU2());
    JavaField* field = constructField(cl, name, type, access);
    readAttributs(cl, reader, field->attributs);
    if (isStatic(access)) {
      cl->staticFields.push_back(field);
    } else {
      cl->virtualFields.push_back(field);
    }
  }
}

void Jnjvm::readMethods(Class* cl, Reader* reader) {
  unsigned short int nbMethods = reader->readU2();
  JavaCtpInfo* ctpInfo = cl->ctpInfo;
  for (int i = 0; i < nbMethods; i++) {
    unsigned short int access = reader->readU2();
    const UTF8* name = ctpInfo->UTF8At(reader->readU2());
    const UTF8* type = ctpInfo->UTF8At(reader->readU2());
    JavaMethod* meth = constructMethod(cl, name, type, access);
    readAttributs(cl, reader, meth->attributs);
    if (isStatic(access)) {
      cl->staticMethods.push_back(meth);
    } else {
      cl->virtualMethods.push_back(meth);
    }
  }  
}

void Jnjvm::readClass(Class* cl) {

  PRINT_DEBUG(JNJVM_LOAD, 0, COLOR_NORMAL, "; ");
  PRINT_DEBUG(JNJVM_LOAD, 0, LIGHT_GREEN, "reading ");
  PRINT_DEBUG(JNJVM_LOAD, 0, COLOR_NORMAL, "%s::%s\n", printString(),
              cl->printString());

  Reader* reader = vm_new(this, Reader)(cl->bytes);
  uint32 magic = reader->readU4();
  if (magic != Jnjvm::Magic) {
    Jnjvm::error(ClassFormatError, "bad magic number %p", magic);
  }
  cl->minor = reader->readU2();
  cl->major = reader->readU2();
  JavaCtpInfo::read(this, cl, reader);
  JavaCtpInfo* ctpInfo = cl->ctpInfo;
  cl->access = reader->readU2();
  
  const UTF8* thisClassName = 
    ctpInfo->resolveClassName(reader->readU2());
  
  if (!(thisClassName->equals(cl->name))) {
    error(ClassFormatError, "try to load %s and found class named %s",
          cl->printString(), thisClassName->printString());
  }

  readParents(cl, reader);
  readFields(cl, reader);
  readMethods(cl, reader);
  readAttributs(cl, reader, cl->attributs);
}

ArrayUInt8* Jnjvm::openName(const UTF8* utf8) {
  char* asciiz = utf8->UTF8ToAsciiz();
  uint32 alen = strlen(asciiz);
  uint32 nbcp = bootClasspath.size();
  uint32 idx = 0;
  ArrayUInt8* res = 0;

  while ((res == 0) && (idx < nbcp)) {
    char* str = bootClasspath[idx];
    unsigned int strLen = strlen(str);
    char* buf = (char*)alloca(strLen + alen + 16);

    if (str[strLen - 1] == dirSeparator[0]) {
      sprintf(buf, "%s%s.class", str, asciiz);
      res = Reader::openFile(this, buf);
    } else {
      sprintf(buf, "%s.class", asciiz);
      res = Reader::openZip(this, str, buf);
    }
    idx++;
  }

  return res;
}


typedef void (*clinit_t)(Jnjvm* vm);

void Jnjvm::initialiseClass(CommonClass* cl) {
  JavaState* status = cl->getStatus();
  if (cl->isArray || AssessorDesc::bogusClassToPrimitive(cl)) {
    *status = ready;
  } else if (!(*status == ready)) {
    cl->aquire();
    JavaState* status = cl->getStatus();
    if (*status == ready) {
      cl->release();
    } else if (*status >= resolved && *status != clinitParent &&
               *status != inClinit) {
      *status = clinitParent;
      cl->release();
      if (cl->super) {
        cl->super->initialiseClass();
      }
      
      *status = inClinit;
      JavaMethod* meth = cl->lookupMethodDontThrow(clinitName, clinitType, true,
                                                   false);
      
      PRINT_DEBUG(JNJVM_LOAD, 0, COLOR_NORMAL, "; ");
      PRINT_DEBUG(JNJVM_LOAD, 0, LIGHT_GREEN, "clinit ");
      PRINT_DEBUG(JNJVM_LOAD, 0, COLOR_NORMAL, "%s::%s\n", printString(),
                  cl->printString());
      
      ((Class*)cl)->createStaticInstance();
      
      if (meth) {
        JavaObject* exc = 0;
        try{
          clinit_t pred = (clinit_t)meth->compiledPtr();
          pred(JavaThread::get()->isolate);
        } catch(...) {
          exc = JavaThread::getJavaException();
          assert(exc && "no exception?");
          JavaThread::clearException();
        }
        if (exc) {
          if (exc->classOf->isAssignableFrom(Classpath::newException)) {
            JavaThread::get()->isolate->initializerError(exc);
          } else {
            JavaThread::throwException(exc);
          }
        }
      }
      
      *status = ready;
      cl->broadcastClass();
    } else if (*status < resolved) {
      cl->release();
      unknownError("try to clinit a not-readed class...");
    } else {
      if (!cl->ownerClass()) {
        while (*status < ready) cl->waitClass();
        cl->release();
        initialiseClass(cl);
      } 
      cl->release();
    }
  }
}

void Jnjvm::resolveClass(CommonClass* cl, bool doClinit) {
  if (cl->status < resolved) {
    cl->aquire();
    int status = cl->status;
    if (status >= resolved) {
      cl->release();
    } else if (status <  loaded) {
      cl->release();
      unknownError("try to resolve a not-readed class");
    } else if (status == loaded || cl->ownerClass()) {
      if (cl->isArray) {
        ClassArray* arrayCl = (ClassArray*)cl;
        CommonClass* baseClass =  arrayCl->baseClass();
        baseClass->resolveClass(doClinit);
        cl->status = resolved;
      } else {
        readClass((Class*)cl);
        cl->status = readed;
        cl->release();
        loadParents((Class*)cl);
        cl->aquire();
        cl->status = prepared;
        ((Class*)cl)->resolveFields();
        cl->status = resolved;
      }
      cl->release();
    } else {
      while (status < resolved) {
        cl->waitClass();
      }
      cl->release();
    }
  }
  if (doClinit) cl->initialiseClass();
}

CommonClass* Jnjvm::loadName(const UTF8* name, JavaObject* loader,
                              bool doResolve, bool doClinit, bool doThrow) {
 

  CommonClass* cl = lookupClass(name, loader);
  const UTF8* bootstrapName = name;
  
  if (!cl || cl->status == hashed) {
    if (!loader) { // I have to load it
      ArrayUInt8* bytes = openName(name);
      if (bytes) {
        if (!cl) cl = bootstrapVM->constructClass(bootstrapName, loader);
        if (cl->status == hashed) {
          cl->aquire();
          if (cl->status == hashed) {
            cl->status = loaded;
            ((Class*)cl)->bytes = bytes;
          }
          cl->release();
        }
      } else {
        cl = 0;
      }
    } else {
      cl = loadInClassLoader(name->internalToJava(this, 0, name->size), loader);
    }
  }

  if (!cl && doThrow) {
    if (!memcmp(name->UTF8ToAsciiz(), NoClassDefFoundError, 
                    strlen(NoClassDefFoundError))) {
      unknownError("Unable to load NoClassDefFoundError");
    }
    Jnjvm::error(NoClassDefFoundError, "unable to load %s", name->printString());
  }

  if (cl && doResolve) cl->resolveClass(doClinit);

  return cl;
}

void Jnjvm::errorWithExcp(const char* className, const JavaObject* excp) {
  Class* cl = (Class*) this->loadName(this->asciizConstructUTF8(className),
                                      CommonClass::jnjvmClassLoader,
                                      true, true, true);
  JavaObject* obj = (*cl)(this);
  JavaJIT::invokeOnceVoid(this, CommonClass::jnjvmClassLoader, className, "<init>",
                          "(Ljava/lang/Throwable;)V", ACC_VIRTUAL, obj, excp);
  JavaThread::throwException(obj);
}

void Jnjvm::error(const char* className, const char* fmt, ...) {
  char* tmp = (char*)alloca(4096);
  Class* cl = (Class*) this->loadName(this->asciizConstructUTF8(className),
                                      CommonClass::jnjvmClassLoader,
                                      true, true, true);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, 4096, fmt, ap);
  va_end(ap);

  JavaObject* obj = (*cl)(this);
  JavaJIT::invokeOnceVoid(this, CommonClass::jnjvmClassLoader, className, "<init>",
                          "(Ljava/lang/String;)V", ACC_VIRTUAL, obj, 
                          this->asciizToStr(tmp));
  JavaThread::throwException(obj);
}


void Jnjvm::verror(const char* className, const char* fmt, va_list ap) {
  char* tmp = (char*)alloca(4096);
  Class* cl = (Class*) this->loadName(this->asciizConstructUTF8(className),
                                      CommonClass::jnjvmClassLoader,
                                      true, true, true);
  vsnprintf(tmp, 4096, fmt, ap);
  va_end(ap);
  JavaObject* obj = (*cl)(this);
  JavaJIT::invokeOnceVoid(this, CommonClass::jnjvmClassLoader, className, "<init>",
                          "(Ljava/lang/String;)V", ACC_VIRTUAL, obj, 
                          this->asciizToStr(tmp));

  JavaThread::throwException(obj);
}

void Jnjvm::arrayStoreException() {
  error(ArrayStoreException, "");
}

void Jnjvm::indexOutOfBounds(const JavaObject* obj, sint32 entry) {
  error(ArrayIndexOutOfBoundsException, "%d", entry);
}

void Jnjvm::negativeArraySizeException(sint32 size) {
  error(NegativeArraySizeException, "%d", size);
}

void Jnjvm::nullPointerException(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char* val = va_arg(ap, char*);
  va_end(ap);
  error(NullPointerException, fmt, val);
}

void Jnjvm::illegalAccessException(const char* msg) {
  error(IllegalAccessException, msg);
}

void Jnjvm::illegalMonitorStateException(const JavaObject* obj) {
  error(IllegalMonitorStateException, "");
}

void Jnjvm::interruptedException(const JavaObject* obj) {
  error(InterruptedException, "");
}


void Jnjvm::initializerError(const JavaObject* excp) {
  errorWithExcp(ExceptionInInitializerError, excp);
}

void Jnjvm::invocationTargetException(const JavaObject* excp) {
  errorWithExcp(InvocationTargetException, excp);
}

void Jnjvm::outOfMemoryError(sint32 n) {
  error(OutOfMemoryError, "");
}

void Jnjvm::illegalArgumentExceptionForMethod(JavaMethod* meth, 
                                               CommonClass* required,
                                               CommonClass* given) {
  error(IllegalArgumentException, "for method %s", meth->printString());
}

void Jnjvm::illegalArgumentExceptionForField(JavaField* field, 
                                              CommonClass* required,
                                              CommonClass* given) {
  error(IllegalArgumentException, "for field %s", field->printString());
}

void Jnjvm::illegalArgumentException(const char* msg) {
  error(IllegalArgumentException, msg);
}

void Jnjvm::classCastException(const char* msg) {
  error(ClassCastException, msg);
}

void Jnjvm::unknownError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  verror(UnknownError, fmt, ap);
}

CommonClass* Jnjvm::lookupClassFromUTF8(const UTF8* utf8, unsigned int start,
                                         unsigned int len, JavaObject* loader,
                                         bool doResolve, bool doClinit,
                                         bool doThrow) {
  uint32 origLen = len;
  const UTF8* name = utf8->javaToInternal(this, start, len);
  bool doLoop = true;
  CommonClass* ret = 0;

  if (len == 0) {
    return 0;
  } else if (name->at(0) == AssessorDesc::I_TAB) {
    
    while (doLoop) {
      --len;
      if (len == 0) {
        doLoop = false;
      } else {
        ++start;
        if (name->at(start) != AssessorDesc::I_TAB) {
          if (name->at(start) == AssessorDesc::I_REF) {
            uint32 size = (uint32)name->size;
            if ((size == (start + 1)) || (size == (start + 2)) || 
                 (name->at(start + 1) == AssessorDesc::I_TAB) || 
                 (utf8->at(origLen - 1) != AssessorDesc::I_END_REF)) {
              doLoop = false; 
            } else {
              const UTF8* componentName = utf8->javaToInternal(this, start + 1,
                                                               len - 2);
              if (loadName(componentName, loader, doResolve, doClinit,
                           doThrow)) {
                ret = constructArray(name, loader);
                if (doResolve) ret->resolveClass(doClinit);
                doLoop = false;
              } else {
                doLoop = false;
              }
            }
          } else {
            uint16 cur = name->at(start);
            if ((cur == AssessorDesc::I_BOOL || cur == AssessorDesc::I_BYTE ||
                 cur == AssessorDesc::I_CHAR || cur == AssessorDesc::I_SHORT ||
                 cur == AssessorDesc::I_INT || cur == AssessorDesc::I_FLOAT || 
                 cur == AssessorDesc::I_DOUBLE || cur == AssessorDesc::I_LONG)
                && ((uint32)name->size) == start + 1) {

              ret = constructArray(name, loader);
              ret->resolveClass(doClinit);
              doLoop = false;
            } else {
              doLoop = false;
            }
          }
        }
      }
    }

    return ret;

  } else {
    return loadName(name, loader, doResolve, doClinit, doThrow);
  }
}

CommonClass* Jnjvm::lookupClassFromJavaString(JavaString* str,
                                              JavaObject* loader,
                                              bool doResolve, bool doClinit,
                                              bool doThrow) {
  return lookupClassFromUTF8(str->value, str->offset, str->count, loader,
                             doResolve, doClinit, doThrow);
}

CommonClass* Jnjvm::lookupClass(const UTF8* utf8, JavaObject* loader) {
  if (loader) {
#ifndef SERVICE_VM
    ClassMap* map = 
      (ClassMap*)(*Classpath::vmdataClassLoader)(loader).PointerVal;
    if (!map) {
      map = vm_new(this, ClassMap)();
      (*Classpath::vmdataClassLoader)(loader, (JavaObject*)map);
    }
#else
    ClassMap* map = 0;
    ServiceDomain* vm = 
      (ServiceDomain*)(*Classpath::vmdataClassLoader)(loader).PointerVal;
    if (!vm) {
      vm = ServiceDomain::allocateService((JavaIsolate*)Jnjvm::bootstrapVM);
      (*Classpath::vmdataClassLoader)(loader, (JavaObject*)vm);
    }
    map = vm->classes;
#endif
    return map->lookup(utf8);
  } else {
    return bootstrapClasses->lookup(utf8);
  }
}

static CommonClass* arrayDup(const UTF8*& name, Jnjvm *vm) {
  ClassArray* cl = vm_new(vm, ClassArray)();
  cl->initialise(vm, true);
  cl->name = name;
  cl->classLoader = 0;
  cl->_funcs = 0;
  cl->_baseClass = 0;
  cl->super = ClassArray::SuperArray;
  cl->interfaces = ClassArray::InterfacesArray;
  cl->virtualMethods = ClassArray::VirtualMethodsArray;
  cl->staticMethods = ClassArray::StaticMethodsArray;
  cl->virtualFields = ClassArray::VirtualFieldsArray;
  cl->staticFields = ClassArray::StaticFieldsArray;
  cl->depth = 1;
  cl->display.push_back(ClassArray::SuperArray);
  cl->display.push_back(cl);
  cl->access = ACC_FINAL | ACC_ABSTRACT;
  cl->status = loaded;
  return cl;
}

ClassArray* Jnjvm::constructArray(const UTF8* name, JavaObject* loader) {
  if (loader != 0)
    loader = ClassArray::arrayLoader(this, name, loader, 1, name->size - 1);

  if (loader) {
#ifndef SERVICE_VM
    ClassMap* map = 
      (ClassMap*)(*Classpath::vmdataClassLoader)(loader).PointerVal;
    if (!map) {
      map = vm_new(this, ClassMap)();
      (*Classpath::vmdataClassLoader)(loader, (JavaObject*)map);
    }
    ClassArray* res = (ClassArray*)map->lookupOrCreate(name, this, arrayDup);
#else
    ClassMap* map = 0;
    ServiceDomain* vm = 
      (ServiceDomain*)(*Classpath::vmdataClassLoader)(loader).PointerVal;
    if (!vm) {
      vm = ServiceDomain::allocateService((JavaIsolate*)Jnjvm::bootstrapVM);
      (*Classpath::vmdataClassLoader)(loader, (JavaObject*)vm);
    }
    map = vm->classes;
    ClassArray* res = (ClassArray*)map->lookupOrCreate(name, vm, arrayDup);
#endif
    if (!res->classLoader) res->classLoader = loader;
    return res;
  } else {
    return (ClassArray*)bootstrapClasses->lookupOrCreate(name, this, arrayDup);
  }
}


static CommonClass* classDup(const UTF8*& name, Jnjvm *vm) {
  Class* cl = vm_new(vm, Class)();
  cl->initialise(vm, false);
  cl->name = name;
  cl->classLoader = 0;
  cl->bytes = 0;
#ifndef MULTIPLE_VM
  cl->_staticInstance = 0;
#endif
  cl->super = 0;
  cl->ctpInfo = 0;
  return cl;
}

Class* Jnjvm::constructClass(const UTF8* name, JavaObject* loader) {
  if (loader) {
#ifndef SERVICE_VM
    ClassMap* map = 
      (ClassMap*)(*Classpath::vmdataClassLoader)(loader).PointerVal;
    if (!map) {
      map = vm_new(this, ClassMap)();
      (*Classpath::vmdataClassLoader)(loader, (JavaObject*)map);
    }
    Class* res = (Class*)map->lookupOrCreate(name, this, classDup);
#else
    ClassMap* map = 0;
    ServiceDomain* vm = 
      (ServiceDomain*)(*Classpath::vmdataClassLoader)(loader).PointerVal;
    if (!vm) {
      vm = ServiceDomain::allocateService((JavaIsolate*)Jnjvm::bootstrapVM);
      (*Classpath::vmdataClassLoader)(loader, (JavaObject*)vm);
    }
    map = vm->classes;
    Class* res = (Class*)map->lookupOrCreate(name, vm, classDup);
#endif
    if (!res->classLoader) res->classLoader = loader;
    return res;
  } else {
    return (Class*)bootstrapClasses->lookupOrCreate(name, this, classDup);
  }
}

static JavaField* fieldDup(FieldCmp & cmp, Jnjvm *vm) {
  JavaField* field = vm_new(vm, JavaField)();
  field->name = cmp.name;
  field->type = cmp.type;
  field->classDef = (Class*)cmp.classDef;
  field->signature = vm->constructType(field->type);
  field->ptrOffset = 0;
  return field;
}

JavaField* Jnjvm::constructField(Class* cl, const UTF8* name, const UTF8* type,
                                  uint32 access){
  FieldCmp CC(name, cl, type, 0);
  JavaField* f = loadedFields->lookupOrCreate(CC, this, fieldDup); 
  f->access = access;
  f->offset = 0;
  return f;
}

JavaField* Jnjvm::lookupField(CommonClass* cl, const UTF8* name, 
                              const UTF8* type) {
  FieldCmp CC(name, cl, type, 0);
  JavaField* f = loadedFields->lookup(CC); 
  return f;
}

static JavaMethod* methodDup(FieldCmp & cmp, Jnjvm *vm) {
  JavaMethod* method = vm_new(vm, JavaMethod)();
  method->name = cmp.name;
  method->type = cmp.type;
  method->classDef = (Class*)cmp.classDef;
  method->signature = (Signdef*)vm->constructType(method->type);
  method->code = 0;
  method->access = cmp.access;
  if (isStatic(method->access)) {
    method->llvmType =method->signature->staticType;
  } else {
    method->llvmType = method->signature->virtualType;
  }
  method->classDef->isolate->protectModule->lock();
  method->llvmFunction = 
    llvm::Function::Create(method->llvmType, llvm::GlobalValue::GhostLinkage,
                           method->printString(),
                           method->classDef->isolate->module);
  method->classDef->isolate->protectModule->unlock();
  method->classDef->isolate->functionDefs->hash(method->llvmFunction, method);
  return method;
}

JavaMethod* Jnjvm::constructMethod(Class* cl, const UTF8* name,
                                    const UTF8* type, uint32 access) {
  FieldCmp CC(name, cl, type, access);
  JavaMethod* f = loadedMethods->lookupOrCreate(CC, this, methodDup);
  return f;
}

const UTF8* Jnjvm::asciizConstructUTF8(const char* asciiz) {
  return hashUTF8->lookupOrCreateAsciiz(this, asciiz);
}

const UTF8* Jnjvm::readerConstructUTF8(const uint16* buf, uint32 size) {
  return hashUTF8->lookupOrCreateReader(this, buf, size);
}

Typedef* Jnjvm::constructType(const UTF8* name) {
  Typedef* res = javaTypes->lookup(name);
  if (res == 0) {
    res = Typedef::typeDup(name, this);
    javaTypes->lock->lock();
    Typedef* tmp = javaTypes->lookup(name);
    if (tmp == 0) javaTypes->hash(name, res);
    else res = tmp;
    javaTypes->lock->unlock();
  }
  return res;
}

CommonClass* Jnjvm::loadInClassLoader(const UTF8* name, JavaObject* loader) {
  JavaString* str = this->UTF8ToStr(name);
  JavaObject* obj = (JavaObject*)
    Classpath::loadInClassLoader->invokeJavaObjectVirtual(this, loader, str);
  return (CommonClass*)((*Classpath::vmdataClass)(obj).PointerVal);
}

JavaString* Jnjvm::UTF8ToStr(const UTF8* utf8) { 
  JavaString* res = hashStr->lookupOrCreate(utf8, this, JavaString::stringDup);
  return res;
}

JavaString* Jnjvm::asciizToStr(const char* asciiz) {
  const UTF8* var = asciizConstructUTF8(asciiz);
  return UTF8ToStr(var);
}

void Jnjvm::addProperty(char* key, char* value) {
  postProperties.push_back(std::make_pair(key, value));
}

#ifndef MULTIPLE_VM
JavaObject* Jnjvm::getClassDelegatee(CommonClass* cl) {
  cl->aquire();
  if (!(cl->delegatee)) {
    JavaObject* delegatee = (*Classpath::newClass)(this);
    cl->delegatee = delegatee;
    Classpath::initClass->invokeIntSpecial(this, delegatee, cl);
  } else if (cl->delegatee->classOf != Classpath::newClass) {
    JavaObject* pd = cl->delegatee;
    JavaObject* delegatee = (*Classpath::newClass)(this);
    cl->delegatee = delegatee;;
    Classpath::initClassWithProtectionDomain->invokeIntSpecial(this, delegatee,
                                                               cl, pd);
  }
  cl->release();
  return cl->delegatee;
}
#else
JavaObject* Jnjvm::getClassDelegatee(CommonClass* cl) {
  cl->aquire();
  JavaObject* val = delegatees->lookup(cl);
  if (!val) {
    val = (*Classpath::newClass)(this);
    delegatees->hash(cl, val);
    Classpath::initClass->invokeIntSpecial(this, val, cl);
  } else if (val->classOf != Classpath::newClass) {
    JavaObject* pd = val;
    val = (*Classpath::newClass)(this);
    delegatees->remove(cl);
    delegatees->hash(cl, val);
    Classpath::initClassWithProtectionDomain->invokeIntSpecial(this, val, cl,
                                                               pd);
  }
  cl->release();
  return val;
}
#endif

void Jnjvm::destroyer(size_t sz) {
#ifdef MULTIPLE_GC
  GC->destroy();
  delete GC;
#endif
  mvm::jit::protectEngine->lock();
  mvm::jit::executionEngine->removeModuleProvider(TheModuleProvider);
  mvm::jit::protectEngine->unlock();
  delete globalRefsLock;
  delete protectModule;
  delete TheModuleProvider;
  delete module;
}
