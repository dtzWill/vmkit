//===- ClasspathVMSystem/Properties.cpp -----------------------------------===//
//===--------------------- GNU classpath gnu/classpath/VMSystemProperties -===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <string.h>
#include <sys/utsname.h>

#include "llvm/Type.h"

#include "types.h"

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaObject.h"
#include "JavaTypes.h"
#include "JavaThread.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "NativeUtil.h"

using namespace jnjvm;

extern "C" {

static void setProperty(Jnjvm* vm, JavaObject* prop, const char* key,
                        const char* val) {
  Classpath::setProperty->invokeIntSpecial(vm, prop, vm->asciizToStr(key),
                                           vm->asciizToStr(val));
}

static void setUnameProp(Jnjvm* vm, JavaObject* prop) {
  struct utsname infos;
  uname(&infos);
  setProperty(vm, prop, "os.name", infos.sysname);
  setProperty(vm, prop, "os.arch", infos.machine);
  setProperty(vm, prop, "os.version", infos.version);
  if (!strcmp(infos.machine, "ppc")) {
    setProperty(vm, prop, "gnu.cpu.endian","big");
  } else {
    setProperty(vm, prop, "gnu.cpu.endian","little");
  }
}

JNIEXPORT void JNICALL Java_gnu_classpath_VMSystemProperties_preInit(
                                    
#ifdef NATIVE_JNI
JNIEnv *env,
jclass clazz,
#endif
jobject _prop) {

  JavaObject* prop = (JavaObject*)_prop;
  Jnjvm* vm = JavaThread::get()->isolate;
  const char* tmp;
  setProperty(vm, prop, "java.vm.specification.version", "1.0");
  setProperty(vm, prop, "java.vm.specification.vendor", "Sun Microsystems, Inc");
  setProperty(vm, prop, "java.vm.specification.name", "Java Virtual Machine Specification");
  setProperty(vm, prop, "java.specification.version", "1.4");
  setProperty(vm, prop, "java.specification.vendor", "Sun Microsystems, Inc");
  setProperty(vm, prop, "java.specification.name", "Java Platform API Specification");
  setProperty(vm, prop, "java.version", "1.4");
  setProperty(vm, prop, "java.runtime.version", "1.4");
  setProperty(vm, prop, "java.vendor", "VVM Project");
  setProperty(vm, prop, "java.vendor.url", "http://vvm.lip6.fr");
  
  tmp = getenv("JAVA_HOME");
  if (!tmp) tmp = "";
  setProperty(vm, prop, "java.home", tmp);
			
  setProperty(vm, prop, "java.class.version", "48.0");
  setProperty(vm, prop, "java.class.path", vm->classpath);
  setProperty(vm, prop, "java.boot.class.path", vm->bootClasspathEnv);
  setProperty(vm, prop, "sun.boot.class.path", vm->bootClasspathEnv);
  setProperty(vm, prop, "java.vm.version", "2.0");
  setProperty(vm, prop, "java.vm.vendor", "VVM Project");
  setProperty(vm, prop, "java.vm.name", "JnJVM");
  setProperty(vm, prop, "java.specification.version", "1.4");
  setProperty(vm, prop, "java.library.path", vm->libClasspathEnv);
  setProperty(vm, prop, "java.io.tmpdir", "/tmp");
  
  tmp = getenv("JAVA_COMPILER");
  if (!tmp) tmp = "gcj";
  setProperty(vm, prop, "java.compiler", tmp);
  
  setProperty(vm, prop, "build.compiler", "gcj");
  setProperty(vm, prop, "gcj.class.path", vm->bootClasspathEnv);
  
  setUnameProp(vm, prop);
  
  setProperty(vm, prop, "file.separator", vm->dirSeparator);
  setProperty(vm, prop, "path.separator", vm->envSeparator);
  setProperty(vm, prop, "line.separator", "\n");
  
  tmp = getenv("USERNAME");
  if (!tmp) tmp = getenv("LOGNAME");
  else if (!tmp) tmp = getenv("NAME");
  else if (!tmp) tmp = "";
  setProperty(vm, prop, "user.name", tmp);
  
  tmp  = getenv("HOME");
  if (!tmp) tmp = "";
  setProperty(vm, prop, "user.home", tmp);
  
  tmp = getenv("PWD");
  if (!tmp) tmp = "";
  setProperty(vm, prop, "user.dir", tmp);
  
  //setProperty(vm, prop, "gnu.classpath.nio.charset.provider.iconv", "true")
  
  setProperty(vm, prop, "file.encoding", "ISO8859_1");


}



}
