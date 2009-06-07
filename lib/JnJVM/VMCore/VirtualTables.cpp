//===--- VirtualTables.cpp - Virtual methods for JnJVM objects ------------===//
//
//                          The VMKit project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// 
// This file contains GC specific tracing functions. It is used by the
// GCMmap2 garbage collector and may be of use for other GCs. Boehm GC does
// not use these functions.
//
// The file is divided into four parts:
// (1) Declaration of internal GC classes.
// (2) Tracing Java objects: regular object, native array, object array.
// (3) Tracing a class loader, which involves tracing the Java objects
//     referenced by classes.
// (4) Tracing the roots of a program: the JVM and the threads.
//
//===----------------------------------------------------------------------===//

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaObject.h"
#include "JavaString.h"
#include "JavaThread.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "JnjvmClassLoader.h"
#include "LockedMap.h"

using namespace jnjvm;

//===----------------------------------------------------------------------===//
// List of classes that will be GC-allocated. One should try to keep this
// list as minimal as possible, and a GC class must be defined only if
// absolutely necessary. If there is an easy way to avoid it, do it! Only
// Java classes should be GC classes.
// Having many GC classes gives more work to the GC for the scanning phase
// and for the relocation phase (for copying collectors.
//
// In JnJVM, we identified two cases where we really need to declare GC
// classes: the fat lock object and the class loader.
//
// For the fat lock there are many design decisions that we could make to
// make it a non-GC class. We leave this as a TODO.
//
// For the class loader, we decided that this was the best solution because
// otherwise it would involve hacks on the java.lang.Classloader class.
// Therefore, we create a new GC class with a finalize method that will
// delete the internal class loader when the Java object class loader is
// not reachable anymore. This also relies on the java.lang.Classloader class
// referencing an object of type VMClassLoader (this is the case in GNU
// Classpath with the vmdata field).
//===----------------------------------------------------------------------===//

VirtualTable VMClassLoader::VT((uintptr_t)VMClassLoader::staticDestructor,
                               (uintptr_t)VMClassLoader::staticDestructor,
                               (uintptr_t)VMClassLoader::staticTracer);

VirtualTable LockObj::VT((uintptr_t)LockObj::staticDestructor,
                         (uintptr_t)LockObj::staticDestructor,
                         (uintptr_t)VirtualTable::emptyTracer);

//===----------------------------------------------------------------------===//
// Empty tracer for static tracers of classes that do not declare static
// variables.
//===----------------------------------------------------------------------===//

extern "C" void EmptyTracer(void*) {}

//===----------------------------------------------------------------------===//
// Trace methods for Java objects. There are four types of objects:
// (1) Base object whose class is not an array: needs to trace the classloader
//     and the lock.
// (1) Object whose class is not an array: needs to trace the classloader, the
//     lock and all the virtual fields.
// (2) Object whose class is an array of objects: needs to trace root (1) and
//     all elements in the array.
// (3) Object whose class is a native array: only needs to trace the lock. The
//     classloader is the bootstrap loader and is traced by the JVM.
//===----------------------------------------------------------------------===//


/// Method for scanning the root of an object.
extern "C" void JavaObjectTracer(JavaObject* obj) {
  obj->traceLock();
  
  CommonClass* cl = obj->getClass();
  assert(cl && "No class");
  cl->classLoader->getJavaClassLoader()->markAndTrace();
}

/// Method for scanning an array whose elements are JavaObjects. This method is
/// called by all non-native Java arrays.
extern "C" void ArrayObjectTracer(ArrayObject* obj) {
  obj->traceLock();
  
  CommonClass* cl = obj->getClass();
  assert(cl && "No class");
  cl->classLoader->getJavaClassLoader()->markAndTrace();
  

  for (sint32 i = 0; i < obj->size; i++) {
    if (obj->elements[i]) obj->elements[i]->markAndTrace();
  } 
}

/// Method for scanning a native array. Only scan the lock. The classloader of
/// the class is the bootstrap loader and therefore does not need to be
/// scanned here.
extern "C" void JavaArrayTracer(JavaArray* obj) {
  obj->traceLock();
}

/// Method for scanning regular objects.
extern "C" void RegularObjectTracer(JavaObject* obj) {
  obj->traceLock();

  Class* cl = obj->getClass()->asClass();
  assert(cl && "Not a class in regular tracer");
  cl->classLoader->getJavaClassLoader()->markAndTrace();

  while (cl->super != 0) {
    for (uint32 i = 0; i < cl->nbVirtualFields; ++i) {
      JavaField& field = cl->virtualFields[i];
      if (field.isReference()) {
        JavaObject* ptr = field.getObjectField(obj);
        ptr->markAndTrace();
      }
    }
    cl = cl->super;
  }
}


//===----------------------------------------------------------------------===//
// Support for scanning Java objects referenced by classes. All classes must
// trace:
// (1) The classloader of the parents (super and interfaces) as well as its
//     own class loader.
// (2) The delegatee object (java.lang.Class) if it exists.
//
// Additionaly, non-primitive and non-array classes must trace:
// (3) The bytes that represent the class file.
// (4) The static instance.
//===----------------------------------------------------------------------===//


void CommonClass::tracer() {
  
  if (super && super->classLoader) {
    JavaObject* Obj = super->classLoader->getJavaClassLoader();
    if (Obj) Obj->markAndTrace();
  
    for (uint32 i = 0; i < nbInterfaces; ++i) {
      if (interfaces[i]->classLoader)
        interfaces[i]->classLoader->getJavaClassLoader()->markAndTrace();
    }
  }

  if (classLoader)
    classLoader->getJavaClassLoader()->markAndTrace();

  for (uint32 i = 0; i < NR_ISOLATES; ++i) {
    // If the delegatee was static allocated, we want to trace its fields.
    if (delegatee[i]) {
      delegatee[i]->tracer();
      delegatee[i]->markAndTrace();
    }
  }
}

void Class::tracer() {
  CommonClass::tracer();
  bytes->markAndTrace();
  
  for (uint32 i =0; i < NR_ISOLATES; ++i) {
    TaskClassMirror &M = IsolateInfo[i];
    if (M.staticInstance && staticTracer != EmptyTracer) {
      for (uint32 i = 0; i < nbStaticFields; ++i) {
        JavaField& field = staticFields[i];
        if (field.isReference()) {
          JavaObject* ptr = field.getObjectField(M.staticInstance);
          ptr->markAndTrace();
        }
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Support for scanning a classloader. A classloader must trace:
// (1) All the classes it has loaded (located in the classmap).
// (2) All the class it has initiated loading and therefore references (located
//     in the classmap).
// (3) All the strings referenced in class files.
//
// The class loader does not need to trace its java.lang.Classloader Java object
// because if we end up here, this means that the Java object is already being
// scanned. Only the Java object traces the class loader.
//
// Additionaly, the bootstrap loader must trace:
// (4) The delegatees of native array classes. Since these classes are not in
//     the class map and they are not GC-allocated, we must trace the objects
//     referenced by the delegatees.
//===----------------------------------------------------------------------===//

void JnjvmClassLoader::tracer() {
  
  for (ClassMap::iterator i = classes->map.begin(), e = classes->map.end();
       i!= e; ++i) {
    CommonClass* cl = i->second;
    if (cl->isClass()) cl->asClass()->tracer();
    else cl->tracer();
  }
  
  for (std::vector<JavaString*,
       gc_allocator<JavaString*> >::iterator i = strings.begin(), 
       e = strings.end(); i!= e; ++i) {
    (*i)->markAndTrace();
    // If the string was static allocated, we want to trace its lock.
    LockObj* l = (*i)->lockObj();
    if (l) l->markAndTrace();
  }
  
}

void JnjvmBootstrapLoader::tracer() {
 
  JnjvmClassLoader::tracer();

#define TRACE_DELEGATEE(prim) \
  prim->tracer();

  TRACE_DELEGATEE(upcalls->OfVoid);
  TRACE_DELEGATEE(upcalls->OfBool);
  TRACE_DELEGATEE(upcalls->OfByte);
  TRACE_DELEGATEE(upcalls->OfChar);
  TRACE_DELEGATEE(upcalls->OfShort);
  TRACE_DELEGATEE(upcalls->OfInt);
  TRACE_DELEGATEE(upcalls->OfFloat);
  TRACE_DELEGATEE(upcalls->OfLong);
  TRACE_DELEGATEE(upcalls->OfDouble);
#undef TRACE_DELEGATEE
}

//===----------------------------------------------------------------------===//
// Support for scanning the roots of a program: JVM and threads. The JVM
// must trace:
// (1) The bootstrap class loader: where core classes live.
// (2) The applicative class loader: the JVM may be the ony one referencing it.
// (3) Global references from JNI.
//
// The threads must trace:
// (1) Their stack (already done by the GC in the case of GCMmap2 or Boehm)
// (2) Their pending exception if there is one.
// (3) The java.lang.Thread delegate.
//===----------------------------------------------------------------------===//


void Jnjvm::tracer() {
  bootstrapLoader->tracer();
  
  if (appClassLoader) appClassLoader->getJavaClassLoader()->markAndTrace();
  
  for (std::vector<JavaObject*, gc_allocator<JavaObject*> >::iterator 
       i = globalRefs.begin(), e = globalRefs.end(); i!= e; ++i) {
    (*i)->markAndTrace(); 
  }
  
  for (StringMap::iterator i = hashStr.map.begin(), e = hashStr.map.end();
       i!= e; ++i) {
    JavaString* str = i->second;
    str->markAndTrace();
  }

#if defined(ISOLATE_SHARING)
  JnjvmSharedLoader::sharedLoader->markAndTrace();
#endif
  
#ifdef SERVICE
  parent->tracer();
#endif
}

void JavaThread::tracer() {
  if (pendingException) pendingException->markAndTrace();
  javaThread->markAndTrace();
#ifdef SERVICE
  ServiceException->markAndTrace();
#endif
}
