//===------------ JavaIsolate.cpp - Start an isolate ----------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <jni.h>

#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetData.h"

#include "mvm/JIT.h"
#include "mvm/Threads/Locks.h"
#include "mvm/Threads/Cond.h"

#include "JavaClass.h"
#include "JavaIsolate.h"
#include "JavaJIT.h"
#include "JavaString.h"
#include "JavaTypes.h"
#include "JavaUpcalls.h"
#include "JnjvmModuleProvider.h"
#include "LockedMap.h"
#include "Zip.h"

#define PATH_MANIFEST "META-INF/MANIFEST.MF"
#define MAIN_CLASS "Main-Class: "
#define PREMAIN_CLASS "Premain-Class: "
#define BOOT_CLASS_PATH "Boot-Class-Path: "
#define CAN_REDEFINE_CLASS_PATH "Can-Redefine-Classes: "

#define LENGTH_MAIN_CLASS 12
#define LENGTH_PREMAIN_CLASS 15
#define LENGTH_BOOT_CLASS_PATH 17

using namespace jnjvm;

extern "C" struct JNINativeInterface JNI_JNIEnvTable;
extern "C" const struct JNIInvokeInterface JNI_JavaVMTable;


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

void ClArgumentsInfo::javaAgent(char* cur) {
  assert(0 && "implement me");
}

extern "C" int strnstr(const char *haystack, const char *needle) {
  char * res = strstr(haystack, needle);
  if (res) return res - haystack;
  else return -1; 
}


static char* findInformation(ArrayUInt8* manifest, const char* entry, uint32 len) {
  uint8* ptr = manifest->elements;
  sint32 index = strnstr((char*)ptr, entry);
  if (index != -1) {
    index += len;
    sint32 end = strnstr((char*)&(ptr[index]), "\n");
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

  ZipArchive* archive = ZipArchive::singleArchive(jarFile);
  if (archive) {
    ZipFile* file = archive->getFile(PATH_MANIFEST);
    if (file) {
      ArrayUInt8* res = ArrayUInt8::acons(file->ucsize, JavaArray::ofByte);
      int ok = archive->readFile(res, file);
      if (ok) {
        char* mainClass = findInformation(res, MAIN_CLASS, LENGTH_MAIN_CLASS);
        archive->remove();
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
    } else if (!(strcmp(cur, "-D"))) {
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


void ThreadSystem::print(mvm::PrintBuffer* buf) const {
  buf->write("ThreadSystem<>");
}

void JavaIsolate::print(mvm::PrintBuffer* buf) const {
  buf->write("Java isolate: ");
  buf->write(name);

}

ThreadSystem* ThreadSystem::allocateThreadSystem() {
  ThreadSystem* res = gc_new(ThreadSystem)();
  res->nonDaemonThreads = 1;
  res->nonDaemonLock = mvm::Lock::allocNormal();
  res->nonDaemonVar  = mvm::Cond::allocCond();
  return res;
}

JavaObject* JavaIsolate::loadAppClassLoader() {
  if (appClassLoader == 0) {
    appClassLoader = Classpath::getSystemClassLoader->invokeJavaObjectStatic();
  }
  return appClassLoader;
}

void JavaIsolate::mapInitialThread() {
  ClasspathThread::mapInitialThread(this);
}

void JavaIsolate::loadBootstrap() {
  mapInitialThread();
  loadAppClassLoader();
  Classpath::setContextClassLoader->invokeIntSpecial(JavaThread::currentThread(), appClassLoader);
  // load and initialise math since it is responsible for dlopen'ing 
  // libjavalang.so and we are optimizing some math operations
  loadName(asciizConstructUTF8("java/lang/Math"), 
           CommonClass::jnjvmClassLoader, true, true, true);
}

void JavaIsolate::executeClass(const char* className, ArrayObject* args) {
  try {
    JavaJIT::invokeOnceVoid(this, appClassLoader, className, "main",
                        "([Ljava/lang/String;)V", ACC_STATIC, args);
  }catch(...) {
  }

  JavaObject* exc = JavaThread::get()->pendingException;
  if (exc) {
    JavaThread::clearException();
    JavaObject* obj = JavaThread::currentThread();
    JavaObject* group = (JavaObject*)((*obj)(ClasspathThread::group)).PointerVal;
    try{
      ClasspathThread::uncaughtException->invokeIntSpecial(group, obj, exc);
    }catch(...) {
      printf("Even uncaught exception throwed an exception!\n");
      assert(0);
    }
  }
}

void JavaIsolate::executePremain(const char* className, JavaString* args,
                             JavaObject* instrumenter) {
  JavaJIT::invokeOnceVoid(this, appClassLoader, className, "premain",
          "(Ljava/lang/String;Ljava/lang/instrument/Instrumentation;)V",
          ACC_STATIC, args, instrumenter);
}

void JavaIsolate::waitForExit() { 
  threadSystem->nonDaemonLock->lock();
  --(threadSystem->nonDaemonThreads);
  
  while (threadSystem->nonDaemonThreads) {
    threadSystem->nonDaemonVar->wait(threadSystem->nonDaemonLock);
  }

  threadSystem->nonDaemonLock->unlock();  
  return;
}

void JavaIsolate::runMain(int argc, char** argv) {
  ClArgumentsInfo info;

  info.readArgs(argc, argv, this);
  if (info.className) {
    int pos = info.appArgumentsPos;
    //llvm::cl::ParseCommandLineOptions(pos, argv,
    //                                  " JnJVM Java Virtual Machine\n");
    argv = argv + pos - 1;
    argc = argc - pos + 1;
    
    loadBootstrap();
    
    if (info.agents.size()) {
      assert(0 && "implement me");
      JavaObject* instrumenter = 0;//createInstrumenter();
      for (std::vector< std::pair<char*, char*> >::iterator i = info.agents.begin(), 
              e = info.agents.end(); i!= e; ++i) {
        JavaString* args = asciizToStr(i->second);
        executePremain(i->first, args, instrumenter);
      }
    }

    ArrayObject* args = ArrayObject::acons(argc - 2, JavaArray::ofString);
    for (int i = 2; i < argc; ++i) {
      args->setAt(i - 2, (JavaObject*)asciizToStr(argv[i]));
    }
  
    executeClass(info.className, args);
    waitForExit();
  }
}

void JavaIsolate::runIsolate(const char* className, ArrayObject* args) {
  JavaIsolate *isolate = allocateIsolate(bootstrapVM);
  isolate->loadBootstrap();
  isolate->executeClass(className, args);
  isolate->waitForExit();
}

extern const char* GNUClasspathGlibj;
extern const char* GNUClasspathLibs;

JavaIsolate* JavaIsolate::allocateIsolate(Jnjvm* callingVM) {
  JavaIsolate *isolate= gc_new(JavaIsolate)();
  
  isolate->classpath = getenv("CLASSPATH");
  if (!(isolate->classpath)) {
    isolate->classpath = ".";
  }
  isolate->bootClasspathEnv = getenv("JNJVM_BOOTCLASSPATH");
  if (!(isolate->bootClasspathEnv)) {
    isolate->bootClasspathEnv = GNUClasspathGlibj;
  }
  isolate->libClasspathEnv = getenv("JNJVM_LIBCLASSPATH");
  if (!(isolate->libClasspathEnv)) {
    isolate->libClasspathEnv = GNUClasspathLibs;
  }
  
  isolate->analyseClasspathEnv(isolate->bootClasspathEnv);

  isolate->functions = FunctionMap::allocate();
  isolate->module = new llvm::Module("Isolate JnJVM");
  isolate->protectModule = mvm::Lock::allocNormal();
  isolate->TheModuleProvider = new JnjvmModuleProvider(isolate->module, 
                                                       isolate->functions);  
  JavaJIT::initialiseJITIsolateVM(isolate);
  
  isolate->bootstrapThread = JavaThread::allocate(0, isolate);
  JavaThread::threadKey->set(isolate->bootstrapThread);

  
  isolate->threadSystem = ThreadSystem::allocateThreadSystem();
  isolate->name = "isolate";
  isolate->appClassLoader = 0;
  isolate->jniEnv = &JNI_JNIEnvTable;
  isolate->javavmEnv = &JNI_JavaVMTable;
  
  // We copy so that bootstrap utf8 such as "<init>" are unique
  isolate->hashUTF8 = bootstrapVM->hashUTF8->copy();
  isolate->hashStr = StringMap::allocate();
  isolate->bootstrapClasses = callingVM->bootstrapClasses;
  isolate->loadedMethods = MethodMap::allocate();
  isolate->loadedFields = FieldMap::allocate();
  isolate->javaTypes = jnjvm::TypeMap::allocate(); 
  isolate->globalRefsLock = mvm::Lock::allocNormal();
  isolate->statics = StaticInstanceMap::allocate();  
  isolate->delegatees = DelegateeMap::allocate(); 

  return isolate;
}

JavaIsolate* JavaIsolate::allocateBootstrap() {
  JavaIsolate *isolate= gc_new(JavaIsolate)();
  
  isolate->classpath = getenv("CLASSPATH");
  if (!(isolate->classpath)) {
    isolate->classpath = ".";
  }
  isolate->bootClasspathEnv = getenv("JNJVM_BOOTCLASSPATH");
  if (!(isolate->bootClasspathEnv)) {
    isolate->bootClasspathEnv = GNUClasspathGlibj;
  }
  isolate->libClasspathEnv = getenv("JNJVM_LIBCLASSPATH");
  if (!(isolate->libClasspathEnv)) {
    isolate->libClasspathEnv = GNUClasspathLibs;
  }
  
  isolate->analyseClasspathEnv(isolate->bootClasspathEnv);
  
  isolate->functions = FunctionMap::allocate();
  isolate->protectModule = mvm::Lock::allocNormal();
  isolate->module = new llvm::Module("Bootstrap JnJVM");
  isolate->TheModuleProvider = new JnjvmModuleProvider(isolate->module, 
                                                       isolate->functions);  
  JavaJIT::initialiseJITBootstrapVM(isolate);
  
  isolate->bootstrapThread = JavaThread::allocate(0, isolate);
  JavaThread::threadKey->set(isolate->bootstrapThread);

  isolate->name = "bootstrapVM";
  isolate->appClassLoader = 0;
  isolate->hashUTF8 = UTF8Map::allocate();
  isolate->hashStr = StringMap::allocate();
  isolate->bootstrapClasses = ClassMap::allocate();
  isolate->loadedMethods = MethodMap::allocate();
  isolate->loadedFields = FieldMap::allocate();
  isolate->jniEnv = &JNI_JNIEnvTable;
  isolate->javavmEnv = &JNI_JavaVMTable;
  isolate->globalRefsLock = mvm::Lock::allocNormal();
  isolate->javaTypes = jnjvm::TypeMap::allocate();  
#ifndef SINGLE_VM
  isolate->statics = StaticInstanceMap::allocate();  
  isolate->delegatees = DelegateeMap::allocate(); 
#endif
  
  return isolate;
}

void JavaIsolate::destroyer(size_t sz) {
  mvm::jit::protectEngine->lock();
  mvm::jit::executionEngine->removeModuleProvider(TheModuleProvider);
  mvm::jit::protectEngine->unlock();
  delete TheModuleProvider;
  delete module;
}
