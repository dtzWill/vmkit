//===---------- Jnjvm.cpp - Java virtual machine description --------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define JNJVM_LOAD 0

#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define DEF_UTF8(var) \
  const UTF8* Jnjvm::var = 0
  
  DEF_UTF8(NoClassDefFoundError);
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
  DEF_UTF8(finalize);

#undef DEF_UTF8

const char* Jnjvm::dirSeparator = "/";
const char* Jnjvm::envSeparator = ":";
const unsigned int Jnjvm::Magic = 0xcafebabe;

#ifndef MULTIPLE_VM
/// If we're not in a multi-vm environment, this can be made static.
std::vector<void*> Jnjvm::nativeLibs;
#endif

typedef void (*clinit_t)(Jnjvm* vm);

void Jnjvm::initialiseClass(CommonClass* cl) {
  JavaState* status = cl->getStatus();
  // Primitives are initialized at boot time
  if (cl->isArray) {
    *status = ready;
  } else if (!(*status == ready)) {
    cl->acquire();
    JavaState* status = cl->getStatus();
    if (*status == ready) {
      cl->release();
    } else if (*status >= resolved && *status != clinitParent &&
               *status != inClinit) {
      *status = clinitParent;
      cl->release();
      if (cl->super) {
        initialiseClass(cl->super);
      }

      cl->classLoader->TheModule->resolveStaticClass((Class*)cl);
      
      *status = inClinit;
      JavaMethod* meth = cl->lookupMethodDontThrow(clinitName, clinitType, true,
                                                   false);
      
      PRINT_DEBUG(JNJVM_LOAD, 0, COLOR_NORMAL, "; ", 0);
      PRINT_DEBUG(JNJVM_LOAD, 0, LIGHT_GREEN, "clinit ", 0);
      PRINT_DEBUG(JNJVM_LOAD, 0, COLOR_NORMAL, "%s::%s\n", printString(),
                  cl->printString());
      
      ((Class*)cl)->createStaticInstance();
      
      if (meth) {
        JavaObject* exc = 0;
        try{
          clinit_t pred = (clinit_t)(intptr_t)meth->compiledPtr();
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
      unknownError("try to clinit a not-read class...");
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


void Jnjvm::errorWithExcp(Class* cl, JavaMethod* init, const JavaObject* excp) {
  JavaObject* obj = cl->doNew(this);
  init->invokeIntSpecial(this, obj, excp);
  JavaThread::throwException(obj);
}

void Jnjvm::error(Class* cl, JavaMethod* init, const char* fmt, ...) {
  char* tmp = (char*)alloca(4096);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, 4096, fmt, ap);
  va_end(ap);
  
  JavaObject* obj = cl->doNew(this);
  init->invokeIntSpecial(this, obj, asciizToStr(tmp));
  JavaThread::throwException(obj);
}

void Jnjvm::arrayStoreException() {
  error(Classpath::ArrayStoreException,
        Classpath::InitArrayStoreException, "");
}

void Jnjvm::indexOutOfBounds(const JavaObject* obj, sint32 entry) {
  error(Classpath::ArrayIndexOutOfBoundsException,
        Classpath::InitArrayIndexOutOfBoundsException, "%d", entry);
}

void Jnjvm::negativeArraySizeException(sint32 size) {
  error(Classpath::NegativeArraySizeException,
        Classpath::InitNegativeArraySizeException, "%d", size);
}

void Jnjvm::nullPointerException(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char* val = va_arg(ap, char*);
  va_end(ap);
  error(Classpath::NullPointerException,
        Classpath::InitNullPointerException, fmt, val);
}

void Jnjvm::illegalAccessException(const char* msg) {
  error(Classpath::IllegalAccessException,
        Classpath::InitIllegalAccessException, msg);
}

void Jnjvm::illegalMonitorStateException(const JavaObject* obj) {
  error(Classpath::IllegalMonitorStateException,
        Classpath::InitIllegalMonitorStateException, "");
}

void Jnjvm::interruptedException(const JavaObject* obj) {
  error(Classpath::InterruptedException,
        Classpath::InitInterruptedException, "");
}


void Jnjvm::initializerError(const JavaObject* excp) {
  errorWithExcp(Classpath::ExceptionInInitializerError,
                Classpath::ErrorWithExcpExceptionInInitializerError,
                excp);
}

void Jnjvm::invocationTargetException(const JavaObject* excp) {
  errorWithExcp(Classpath::InvocationTargetException,
                Classpath::ErrorWithExcpInvocationTargetException,
                excp);
}

void Jnjvm::outOfMemoryError(sint32 n) {
  error(Classpath::OutOfMemoryError,
        Classpath::InitOutOfMemoryError, "%d", n);
}

void Jnjvm::illegalArgumentExceptionForMethod(JavaMethod* meth, 
                                               CommonClass* required,
                                               CommonClass* given) {
  error(Classpath::IllegalArgumentException, 
        Classpath::InitIllegalArgumentException, 
        "for method %s", meth->printString());
}

void Jnjvm::illegalArgumentExceptionForField(JavaField* field, 
                                              CommonClass* required,
                                              CommonClass* given) {
  error(Classpath::IllegalArgumentException, 
        Classpath::InitIllegalArgumentException, 
        "for field %s", field->printString());
}

void Jnjvm::illegalArgumentException(const char* msg) {
  error(Classpath::IllegalArgumentException,
        Classpath::InitIllegalArgumentException,
        msg);
}

void Jnjvm::classCastException(const char* msg) {
  error(Classpath::ClassCastException,
        Classpath::InitClassCastException,
        msg);
}

void Jnjvm::noSuchFieldError(CommonClass* cl, const UTF8* name) {
  error(Classpath::NoSuchFieldError,
        Classpath::InitNoSuchFieldError, 
        "unable to find %s in %s",
        name->UTF8ToAsciiz(), cl->printString());

}

void Jnjvm::noSuchMethodError(CommonClass* cl, const UTF8* name) {
  error(Classpath::NoSuchMethodError,
        Classpath::InitNoSuchMethodError, 
        "unable to find %s in %s",
        name->UTF8ToAsciiz(), cl->printString());

}

void Jnjvm::classFormatError(const char* msg, ...) {
  error(Classpath::ClassFormatError,
        Classpath::InitClassFormatError, 
        msg);
}

void Jnjvm::noClassDefFoundError(JavaObject* obj) {
  errorWithExcp(Classpath::NoClassDefFoundError,
        Classpath::ErrorWithExcpNoClassDefFoundError, 
        obj);
}

void Jnjvm::noClassDefFoundError(const char* fmt, ...) {
  error(Classpath::NoClassDefFoundError,
        Classpath::InitNoClassDefFoundError, 
        fmt);
}

void Jnjvm::classNotFoundException(JavaString* str) {
  error(Classpath::ClassNotFoundException,
        Classpath::InitClassNotFoundException, 
        "unable to load %s",
        str->strToAsciiz());
}

void Jnjvm::unknownError(const char* fmt, ...) {
  error(Classpath::UnknownError,
        Classpath::InitUnknownError,  
        fmt);
}

JavaString* Jnjvm::UTF8ToStr(const UTF8* utf8) { 
  JavaString* res = hashStr->lookupOrCreate(utf8, this, JavaString::stringDup);
  return res;
}

JavaString* Jnjvm::asciizToStr(const char* asciiz) {
  // asciizToStr is called by jnjvm code, so utf8s created
  // by this method are stored in the bootstrap class loader
  JnjvmClassLoader* JCL = JnjvmClassLoader::bootstrapLoader;
  const UTF8* var = JCL->asciizConstructUTF8(asciiz);
  return UTF8ToStr(var);
}

void Jnjvm::addProperty(char* key, char* value) {
  postProperties.push_back(std::make_pair(key, value));
}

#ifndef MULTIPLE_VM
JavaObject* Jnjvm::getClassDelegatee(CommonClass* cl, JavaObject* pd) {
  cl->acquire();
  if (!(cl->delegatee)) {
    JavaObject* delegatee = Classpath::newClass->doNew(this);
    cl->delegatee = delegatee;
    if (!pd) {
      Classpath::initClass->invokeIntSpecial(this, delegatee, cl);
    } else {
      Classpath::initClassWithProtectionDomain->invokeIntSpecial(this,
                                                                 delegatee,
                                                                 cl, pd);
    }
  }
  cl->release();
  return cl->delegatee;
}
#else
JavaObject* Jnjvm::getClassDelegatee(CommonClass* cl, JavaObject* pd) {
  cl->acquire();
  JavaObject* val = delegatees->lookup(cl);
  if (!val) {
    val = Classpath::newClass->doNew(this);
    delegatees->hash(cl, val);
    if (!pd) {
      Classpath::initClass->invokeIntSpecial(this, val, cl);
    } else {
      Classpath::initClassWithProtectionDomain->invokeIntSpecial(this, val, cl,
                                                                 pd);
    }
  }
  cl->release();
  return val;
}
#endif

Jnjvm::~Jnjvm() {
#ifdef MULTIPLE_GC
  GC->destroy();
  delete GC;
#endif
  
  delete hashStr;  
  delete globalRefsLock;
  delete threadSystem;
}

Jnjvm::Jnjvm() {
#ifdef MULTIPLE_GC
  GC = 0;
#endif
  hashStr = 0;
  globalRefsLock = 0;
  threadSystem = 0;
}

#define PATH_MANIFEST "META-INF/MANIFEST.MF"
#define MAIN_CLASS "Main-Class: "
#define PREMAIN_CLASS "Premain-Class: "
#define BOOT_CLASS_PATH "Boot-Class-Path: "
#define CAN_REDEFINE_CLASS_PATH "Can-Redefine-Classes: "

#define LENGTH_MAIN_CLASS 12
#define LENGTH_PREMAIN_CLASS 15
#define LENGTH_BOOT_CLASS_PATH 17

extern "C" struct JNINativeInterface JNI_JNIEnvTable;
extern "C" const struct JNIInvokeInterface JNI_JavaVMTable;

namespace jnjvm {

class ClArgumentsInfo {
public:
  uint32 appArgumentsPos;
  char* className;
  std::vector< std::pair<char*, char*> > agents;

  void readArgs(int argc, char** argv, Jnjvm *vm);
  void extractClassFromJar(Jnjvm* vm, int argc, char** argv, int i);
  void javaAgent(char* cur);

  void printInformation();
  void nyi();
  void printVersion();
};

}

void ClArgumentsInfo::javaAgent(char* cur) {
  assert(0 && "implement me");
}

extern "C" int sys_strnstr(const char *haystack, const char *needle) {
  char * res = strstr(haystack, needle);
  if (res) return res - haystack;
  else return -1; 
}


static char* findInformation(ArrayUInt8* manifest, const char* entry, uint32 len) {
  uint8* ptr = manifest->elements;
  sint32 index = sys_strnstr((char*)ptr, entry);
  if (index != -1) {
    index += len;
    sint32 end = sys_strnstr((char*)&(ptr[index]), "\n");
    if (end == -1) end = manifest->size;
    else end += index;

    sint32 length = end - index - 1;
    char* name = (char*)malloc(length + 1);
    memcpy(name, &(ptr[index]), length);
    name[length] = 0;
    return name;
  } else {
    return 0;
  }
}

void ClArgumentsInfo::extractClassFromJar(Jnjvm* vm, int argc, char** argv, 
                                          int i) {
  char* jarFile = argv[i];
  char* temp = 
    (char*)malloc(2 + strlen(vm->classpath) + strlen(jarFile));

  sprintf(temp, "%s:%s", vm->classpath, jarFile);
  vm->setClasspath(temp);
  
  ArrayUInt8* bytes = Reader::openFile(JnjvmClassLoader::bootstrapLoader,
                                       jarFile);

  ZipArchive archive(bytes);
  if (archive.getOfscd() != -1) {
    ZipFile* file = archive.getFile(PATH_MANIFEST);
    if (file) {
      ArrayUInt8* res = ArrayUInt8::acons(file->ucsize, JavaArray::ofByte, &vm->allocator);
      int ok = archive.readFile(res, file);
      if (ok) {
        char* mainClass = findInformation(res, MAIN_CLASS, LENGTH_MAIN_CLASS);
        if (mainClass) {
          className = mainClass;
        } else {
          printf("No Main-Class:  in Manifest of archive %s.\n", jarFile);
        }
      } else {
        printf("Can't extract Manifest file from archive %s\n", jarFile);
      }
    } else {
      printf("Can't find Manifest file in archive %s\n", jarFile);
    }
  } else {
    printf("Can't find archive %s\n", jarFile);
  }
}

void ClArgumentsInfo::nyi() {
  fprintf(stdout, "Not yet implemented\n");
}

void ClArgumentsInfo::printVersion() {
  fprintf(stdout, "JnJVM for Java 1.1 -- 1.5\n");
}

void ClArgumentsInfo::printInformation() {
  fprintf(stdout, 
  "Usage: java [-options] class [args...] (to execute a class)\n"
   "or  java [-options] -jar jarfile [args...]\n"
           "(to execute a jar file) where options include:\n"
    "-client       to select the \"client\" VM\n"
    "-server       to select the \"server\" VM\n"
    "-hotspot      is a synonym for the \"client\" VM  [deprecated]\n"
    "              The default VM is client.\n"
    "\n"
    "-cp <class search path of directories and zip/jar files>\n"
    "-classpath <class search path of directories and zip/jar files>\n"
    "              A : separated list of directories, JAR archives,\n"
    "              and ZIP archives to search for class files.\n"
    "-D<name>=<value>\n"
    "              set a system property\n"
    "-verbose[:class|gc|jni]\n"
    "              enable verbose output\n"
    "-version      print product version and exit\n"
    "-version:<value>\n"
    "              require the specified version to run\n"
    "-showversion  print product version and continue\n"
    "-jre-restrict-search | -jre-no-restrict-search\n"
    "              include/exclude user private JREs in the version search\n"
    "-? -help      print this help message\n"
    "-X            print help on non-standard options\n"
    "-ea[:<packagename>...|:<classname>]\n"
    "-enableassertions[:<packagename>...|:<classname>]\n"
    "              enable assertions\n"
    "-da[:<packagename>...|:<classname>]\n"
    "-disableassertions[:<packagename>...|:<classname>]\n"
    "              disable assertions\n"
    "-esa | -enablesystemassertions\n"
    "              enable system assertions\n"
    "-dsa | -disablesystemassertions\n"
    "              disable system assertions\n"
    "-agentlib:<libname>[=<options>]\n"
    "              load native agent library <libname>, e.g. -agentlib:hprof\n"
    "                see also, -agentlib:jdwp=help and -agentlib:hprof=help\n"
    "-agentpath:<pathname>[=<options>]\n"
    "              load native agent library by full pathname\n"
    "-javaagent:<jarpath>[=<options>]\n"
    "              load Java programming language agent, see java.lang.instrument\n");
}

void ClArgumentsInfo::readArgs(int argc, char** argv, Jnjvm* vm) {
  className = 0;
  appArgumentsPos = 0;
  sint32 i = 1;
  if (i == argc) printInformation();
  while (i < argc) {
    char* cur = argv[i];
    if (!(strcmp(cur, "-client"))) {
      nyi();
    } else if (!(strcmp(cur, "-server"))) {
      nyi();
    } else if (!(strcmp(cur, "-classpath"))) {
      ++i;
      if (i == argc) printInformation();
      else vm->setClasspath(argv[i]);
    } else if (!(strcmp(cur, "-cp"))) {
      ++i;
      if (i == argc) printInformation();
      else vm->setClasspath(argv[i]);
    } else if (!(strcmp(cur, "-debug"))) {
      nyi();
    } else if (!(strncmp(cur, "-D", 2))) {
      uint32 len = strlen(cur);
      if (len == 2) {
        printInformation();
      } else {
        char* key = &cur[2];
        char* value = strchr(key, '=');
        if (!value) {
          printInformation();
          return;
        } else {
          value[0] = 0;
          vm->addProperty(key, &value[1]);
        }
      }
    } else if (!(strncmp(cur, "-Xbootclasspath:", 16))) {
      uint32 len = strlen(cur);
      if (len == 16) {
        printInformation();
      } else {
        char* path = &cur[16];
        JnjvmClassLoader::bootstrapLoader->analyseClasspathEnv(path);
      }
    } else if (!(strcmp(cur, "-enableassertions"))) {
      nyi();
    } else if (!(strcmp(cur, "-ea"))) {
      nyi();
    } else if (!(strcmp(cur, "-disableassertions"))) {
      nyi();
    } else if (!(strcmp(cur, "-da"))) {
      nyi();
    } else if (!(strcmp(cur, "-enablesystemassertions"))) {
      nyi();
    } else if (!(strcmp(cur, "-esa"))) {
      nyi();
    } else if (!(strcmp(cur, "-disablesystemassertions"))) {
      nyi();
    } else if (!(strcmp(cur, "-dsa"))) {
      nyi();
    } else if (!(strcmp(cur, "-jar"))) {
      ++i;
      if (i == argc) {
        printInformation();
      } else {
        extractClassFromJar(vm, argc, argv, i);
        appArgumentsPos = i;
        return;
      }
    } else if (!(strcmp(cur, "-jre-restrict-research"))) {
      nyi();
    } else if (!(strcmp(cur, "-jre-no-restrict-research"))) {
      nyi();
    } else if (!(strcmp(cur, "-noclassgc"))) {
      nyi();
    } else if (!(strcmp(cur, "-ms"))) {
      nyi();
    } else if (!(strcmp(cur, "-mx"))) {
      nyi();
    } else if (!(strcmp(cur, "-ss"))) {
      nyi();
    } else if (!(strcmp(cur, "-verbose"))) {
      nyi();
    } else if (!(strcmp(cur, "-verbose:class"))) {
      nyi();
    } else if (!(strcmp(cur, "-verbosegc"))) {
      nyi();
    } else if (!(strcmp(cur, "-verbose:gc"))) {
      nyi();
    } else if (!(strcmp(cur, "-verbose:jni"))) {
      nyi();
    } else if (!(strcmp(cur, "-version"))) {
      printVersion();
    } else if (!(strcmp(cur, "-showversion"))) {
      nyi();
    } else if (!(strcmp(cur, "-?"))) {
      printInformation();
    } else if (!(strcmp(cur, "-help"))) {
      printInformation();
    } else if (!(strcmp(cur, "-X"))) {
      nyi();
    } else if (!(strcmp(cur, "-agentlib"))) {
      nyi();
    } else if (!(strcmp(cur, "-agentpath"))) {
      nyi();
    } else if (cur[0] == '-') {
    } else if (!(strcmp(cur, "-javaagent"))) {
      javaAgent(cur);
    } else {
      className = cur;
      appArgumentsPos = i;
      return;
    }
    ++i;
  }
}


void Jnjvm::print(mvm::PrintBuffer* buf) const {
  buf->write("Java isolate: ");
  buf->write(name);

}

JnjvmClassLoader* Jnjvm::loadAppClassLoader() {
  if (appClassLoader == 0) {
    JavaObject* loader = Classpath::getSystemClassLoader->invokeJavaObjectStatic(this);
    appClassLoader = JnjvmClassLoader::getJnjvmLoaderFromJavaObject(loader);
  }
  return appClassLoader;
}

void Jnjvm::mapInitialThread() {
  Classpath::mapInitialThread(this);
}

void Jnjvm::loadBootstrap() {
  JnjvmClassLoader* loader = JnjvmClassLoader::bootstrapLoader;
#define LOAD_CLASS(cl) \
  cl->resolveClass(); \
  initialiseClass(cl);

  LOAD_CLASS(Classpath::newClass);
  LOAD_CLASS(Classpath::newConstructor);
  LOAD_CLASS(Classpath::newString);
  LOAD_CLASS(Classpath::newMethod);
  LOAD_CLASS(Classpath::newField);
  LOAD_CLASS(Classpath::newStackTraceElement);
  LOAD_CLASS(Classpath::newVMThrowable);
  LOAD_CLASS(Classpath::InvocationTargetException);
  LOAD_CLASS(Classpath::ArrayStoreException);
  LOAD_CLASS(Classpath::ClassCastException);
  LOAD_CLASS(Classpath::IllegalMonitorStateException);
  LOAD_CLASS(Classpath::IllegalArgumentException);
  LOAD_CLASS(Classpath::InterruptedException);
  LOAD_CLASS(Classpath::IndexOutOfBoundsException);
  LOAD_CLASS(Classpath::ArrayIndexOutOfBoundsException);
  LOAD_CLASS(Classpath::NegativeArraySizeException);
  LOAD_CLASS(Classpath::NullPointerException);
  LOAD_CLASS(Classpath::SecurityException);
  LOAD_CLASS(Classpath::ClassFormatError);
  LOAD_CLASS(Classpath::ClassCircularityError);
  LOAD_CLASS(Classpath::NoClassDefFoundError);
  LOAD_CLASS(Classpath::UnsupportedClassVersionError);
  LOAD_CLASS(Classpath::NoSuchFieldError);
  LOAD_CLASS(Classpath::NoSuchMethodError);
  LOAD_CLASS(Classpath::InstantiationError);
  LOAD_CLASS(Classpath::IllegalAccessError);
  LOAD_CLASS(Classpath::IllegalAccessException);
  LOAD_CLASS(Classpath::VerifyError);
  LOAD_CLASS(Classpath::ExceptionInInitializerError);
  LOAD_CLASS(Classpath::LinkageError);
  LOAD_CLASS(Classpath::AbstractMethodError);
  LOAD_CLASS(Classpath::UnsatisfiedLinkError);
  LOAD_CLASS(Classpath::InternalError);
  LOAD_CLASS(Classpath::OutOfMemoryError);
  LOAD_CLASS(Classpath::StackOverflowError);
  LOAD_CLASS(Classpath::UnknownError);
  LOAD_CLASS(Classpath::ClassNotFoundException); 
#undef LOAD_CLASS

  mapInitialThread();
  loadAppClassLoader();
  JavaObject* obj = JavaThread::currentThread();
  Classpath::setContextClassLoader->invokeIntSpecial(this, obj,
                                        appClassLoader->getJavaClassLoader());
  // load and initialise math since it is responsible for dlopen'ing 
  // libjavalang.so and we are optimizing some math operations
  CommonClass* math = 
    loader->loadName(loader->asciizConstructUTF8("java/lang/Math"), true, true);
  initialiseClass(math);
}

void Jnjvm::executeClass(const char* className, ArrayObject* args) {
  try {
    JavaJIT::invokeOnceVoid(this, appClassLoader, className, "main",
                        "([Ljava/lang/String;)V", ACC_STATIC, args);
  }catch(...) {
  }

  JavaObject* exc = JavaThread::get()->pendingException;
  if (exc) {
    JavaThread::clearException();
    JavaObject* obj = JavaThread::currentThread();
    JavaObject* group = 
      Classpath::group->getVirtualObjectField(obj);
    try{
      Classpath::uncaughtException->invokeIntSpecial(this, group, obj, 
                                                           exc);
    }catch(...) {
      printf("Even uncaught exception throwed an exception!\n");
      assert(0);
    }
  }
}

void Jnjvm::executePremain(const char* className, JavaString* args,
                             JavaObject* instrumenter) {
  JavaJIT::invokeOnceVoid(this, appClassLoader, className, "premain",
          "(Ljava/lang/String;Ljava/lang/instrument/Instrumentation;)V",
          ACC_STATIC, args, instrumenter);
}

void Jnjvm::waitForExit() { 
  threadSystem->nonDaemonLock->lock();
  --(threadSystem->nonDaemonThreads);
  
  while (threadSystem->nonDaemonThreads) {
    threadSystem->nonDaemonVar->wait(threadSystem->nonDaemonLock);
  }

  threadSystem->nonDaemonLock->unlock();  
  return;
}

void Jnjvm::runMain(int argc, char** argv) {
  ClArgumentsInfo info;

  info.readArgs(argc, argv, this);
  if (info.className) {
    int pos = info.appArgumentsPos;
    //llvm::cl::ParseCommandLineOptions(pos, argv,
    //                                  " JnJVM Java Virtual Machine\n");
    argv = argv + pos - 1;
    argc = argc - pos + 1;
    
    loadBootstrap();
#ifdef SERVICE_VM
    ServiceDomain::initialise((ServiceDomain*)this);
#endif
    
    if (info.agents.size()) {
      assert(0 && "implement me");
      JavaObject* instrumenter = 0;//createInstrumenter();
      for (std::vector< std::pair<char*, char*> >::iterator i = 
                                                  info.agents.begin(),
              e = info.agents.end(); i!= e; ++i) {
        JavaString* args = asciizToStr(i->second);
        executePremain(i->first, args, instrumenter);
      }
    }

    ArrayObject* args = ArrayObject::acons(argc - 2, JavaArray::ofString,
                                           &allocator);
    for (int i = 2; i < argc; ++i) {
      args->elements[i - 2] = (JavaObject*)asciizToStr(argv[i]);
    }

    executeClass(info.className, args);
    waitForExit();
  }
}

void Jnjvm::runIsolate(const char* className, ArrayObject* args) {
  int stack;
  Jnjvm *isolate = allocateIsolate(&stack);
  isolate->loadBootstrap();
  isolate->executeClass(className, args);
  isolate->waitForExit();
}

Jnjvm* Jnjvm::allocateIsolate(void* sp) {
  Jnjvm *isolate= gc_new(Jnjvm)();

#ifdef MULTIPLE_GC
  isolate->GC = Collector::allocate();
#endif 
  isolate->classpath = getenv("CLASSPATH");
  if (!(isolate->classpath)) {
    isolate->classpath = ".";
  }
  
  isolate->bootstrapThread = allocator_new(&isolate->allocator, JavaThread)();
  isolate->bootstrapThread->initialise(0, isolate);
  void* baseSP = sp ? sp : mvm::Thread::get()->baseSP;
  JavaThread::threadKey->set(isolate->bootstrapThread);
  
#ifdef MULTIPLE_GC
  isolate->bootstrapThread->GC = isolate->GC;
  isolate->GC->inject_my_thread(baseSP);
#else
  if (sp) Collector::inject_my_thread(baseSP);
#endif

  isolate->bootstrapThread->baseSP = baseSP;
  
  isolate->bootstrapThread->threadID = (mvm::Thread::self() << 8) & 0x7FFFFF00;
  isolate->threadSystem = new ThreadSystem();
  isolate->name = "isolate";
  isolate->appClassLoader = 0;
  isolate->jniEnv = &JNI_JNIEnvTable;
  isolate->javavmEnv = &JNI_JavaVMTable;
  
  isolate->hashStr = new StringMap();
  isolate->globalRefsLock = mvm::Lock::allocNormal();
#ifdef MULTIPLE_VM
  isolate->statics = allocator_new(&isolate->allocator, StaticInstanceMap)(); 
  isolate->delegatees = allocator_new(&isolate->allocator, DelegateeMap)(); 
#endif

  return isolate;
}
