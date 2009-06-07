//===-- JnjvmClassLoader.h - Jnjvm representation of a class loader -------===//
//
//                              Jnjvm
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#ifndef JNJVM_CLASSLOADER_H
#define JNJVM_CLASSLOADER_H

#include <map>
#include <vector>

#include "types.h"

#include "mvm/Allocator.h"
#include "mvm/Object.h"

#include "JnjvmConfig.h"

namespace jnjvm {

class ArrayUInt8;
class UserClass;
class UserClassArray;
class ClassMap;
class Classpath;
class UserCommonClass;
class JavaCompiler;
class JavaMethod;
class JavaObject;
class JavaString;
class Jnjvm;
class JnjvmBootstrapLoader;
class Signdef;
class SignMap;
class Typedef;
class TypeMap;
class UTF8;
class UTF8Map;
class ZipArchive;


/// JnjvmClassLoader - Runtime representation of a class loader. It contains
/// its own tables (signatures, UTF8, types) which are mapped to a single
/// table for non-isolate environments.
///
class JnjvmClassLoader : public mvm::PermanentObject {
private:

  /// isolate - Which isolate defined me? Null for the bootstrap class loader.
  ///
  Jnjvm* isolate;

  /// javaLoder - The Java representation of the class loader. Null for the
  /// bootstrap class loader.
  ///
  JavaObject* javaLoader;

  /// internalLoad - Load the class with the given name.
  ///
  virtual UserClass* internalLoad(const UTF8* utf8, bool doResolve,
                                  JavaString* strName = 0);
  
  /// internalConstructType - Hashes a Typedef, an internal representation of
  /// a class still not loaded.
  ///
  Typedef* internalConstructType(const UTF8 * name);
  
  /// JnjvmClassLoader - Allocate a user-defined class loader. Called on
  /// first use of a Java class loader.
  ///
  JnjvmClassLoader(mvm::BumpPtrAllocator& Alloc, JnjvmClassLoader& JCL,
                   JavaObject* loader, Jnjvm* isolate);

  /// lookupComponentName - Try to find the component name of the given array
  /// name. If the component name is not in the table of UTF8s and holder
  /// is null, the function returns 0.
  ///
  const UTF8* lookupComponentName(const UTF8* name, UTF8* holder, bool& prim);

protected:
  
  JnjvmClassLoader(mvm::BumpPtrAllocator& Alloc) : allocator(Alloc) {}
  
  /// TheCompiler - The Java compiler for this class loader.
  ///
  JavaCompiler* TheCompiler;

  /// classes - The classes this class loader has loaded.
  ///
  ClassMap* classes;
  
  /// javaTypes - Tables of Typedef defined by this class loader.
  ///
  TypeMap* javaTypes;

  /// javaSignatures - Tables of Signdef defined by this class loader.
  ///
  SignMap* javaSignatures;

public:
  
  /// allocator - Reference to the memory allocator, which will allocate UTF8s,
  /// signatures and types.
  ///
  mvm::BumpPtrAllocator& allocator;
 
  /// getIsolate - Returns the isolate that created this class loader.
  ///
  Jnjvm* getIsolate() const { return isolate; }

  /// getClasses - Returns the classes this class loader has loaded.
  ///
  ClassMap* getClasses() const { return classes; }
  
  /// hashUTF8 - Tables of UTF8s defined by this class loader.
  ///
  UTF8Map* hashUTF8;
  
  /// getCompiler - Get the Java compiler of this class loader.
  ///
  JavaCompiler* getCompiler() const { return TheCompiler; }

  /// setCompiler - Set the compiler of classes loaded by this class loader.
  ///
  void setCompiler(JavaCompiler* Comp) { TheCompiler = Comp; }

  /// tracer - Traces a JnjvmClassLoader for GC.
  ///
  virtual void tracer();
  
  /// getJnjvmLoaderFromJavaObject - Return the Jnjvm runtime representation
  /// of the given class loader.
  ///
  static JnjvmClassLoader* getJnjvmLoaderFromJavaObject(JavaObject*, Jnjvm *vm);
  
  /// getJavaClassLoader - Return the Java representation of this class loader.
  ///
  JavaObject* getJavaClassLoader() const {
    return javaLoader;
  }
  
  /// loadName - Loads the class of the given name.
  ///
  UserClass* loadName(const UTF8* name, bool doResolve, bool doThrow,
                      JavaString* strName = 0);
  
  /// loadClassFromUTF8 - Lookup a class from an UTF8 name and load it.
  ///
  UserCommonClass* loadClassFromUserUTF8(const UTF8* utf8,
                                         bool doResolve, bool doThrow,
                                         JavaString* strName = 0);
  
  /// loadClassFromAsciiz - Lookup a class from an asciiz name and load it.
  ///
  UserCommonClass* loadClassFromAsciiz(const char* name,
                                       bool doResolve, bool doThrow);
  
  /// loadClassFromJavaString - Lookup a class from a Java String and load it.
  ///
  UserCommonClass* loadClassFromJavaString(JavaString* str,
                                           bool doResolve, bool doThrow);
  
  /// lookupClassFromJavaString - Finds the class of the given string name in
  /// the class loader's table. Do not inline this function, because it
  /// does an alloca and is called by Classpath functions.
  ///
  UserCommonClass* lookupClassFromJavaString(JavaString* str) 
    __attribute__ ((noinline));
   
  /// lookupClass - Finds the class of the given name in the class loader's
  /// table.
  ///
  UserCommonClass* lookupClass(const UTF8* utf8);
  
  /// lookupClassOrArray - Finds the class of the given name in the class
  /// loader's table. If the class has not been loaded, and if it's an
  /// array whose base class is loaded, then this function loads the array class
  /// and returns it.
  ///
  UserCommonClass* lookupClassOrArray(const UTF8* utf8);

  /// constructArray - Hashes a runtime representation of a class with
  /// the given name.
  ///
  UserClassArray* constructArray(const UTF8* name);
  UserClassArray* constructArray(const UTF8* name, UserCommonClass* base);
  
  UserCommonClass* loadBaseClass(const UTF8* name, uint32 start, uint32 len);

  /// constructClass - Hashes a runtime representation of a class with
  /// the given name.
  ///
  UserClass* constructClass(const UTF8* name, ArrayUInt8* bytes);
  
  /// constructType - Hashes a Typedef, an internal representation of a class
  /// still not loaded.
  ///
  Typedef* constructType(const UTF8 * name);

  /// constructSign - Hashes a Signdef, a method signature.
  ///
  Signdef* constructSign(const UTF8 * name);
  
  /// asciizConstructUTF8 - Hashes an UTF8 created from the given asciiz.
  ///
  const UTF8* asciizConstructUTF8(const char* asciiz);

  /// readerConstructUTF8 - Hashes an UTF8 created from the given Unicode
  /// buffer.
  ///
  const UTF8* readerConstructUTF8(const uint16* buf, uint32 size);
  
  /// bootstrapLoader - The bootstrap loader of the JVM. Loads the base
  /// classes.
  ///
  JnjvmBootstrapLoader* bootstrapLoader;
  
  /// ~JnjvmClassLoader - Destroy the loader: destroy the tables, JIT module and
  /// module provider.
  ///
  virtual ~JnjvmClassLoader();
  
  /// loadClass - The user class that defines the loadClass method.
  ///
  UserClass* loadClass;
 
  /// constructArrayName - Construct an array name based on a class name
  /// and the number of dimensions.
  const UTF8* constructArrayName(uint32 steps, const UTF8* className);
  
  /// UTF8ToStr - Constructs a Java string out of the UTF8.
  ///
  virtual JavaString* UTF8ToStr(const UTF8* utf8);

  /// Strings hashed by this classloader.
  ///
  std::vector<JavaString*, gc_allocator<JavaString*> > strings;
  
  /// nativeLibs - Native libraries (e.g. '.so') loaded by this class loader.
  ///
  std::vector<void*> nativeLibs;

  /// loadInLib - Loads a native function out of the native libraries loaded
  /// by this class loader. The last argument tells if the returned method
  /// is defined in jnjvm.
  ///
  intptr_t loadInLib(const char* buf, bool& jnjvm);

  /// loadInLib - Loads a native function out of the given native library.
  ///
  intptr_t loadInLib(const char* buf, void* handle);

  /// loadLib - Loads the library with the given name.
  ///
  void* loadLib(const char* buf);

  /// nativeLookup - Lookup in the class loader a function pointer for the
  /// method. Also set in the jnjvm parameter is the function is defined in
  /// JnJVM.
  ///
  intptr_t nativeLookup(JavaMethod* meth, bool& jnjvm, char* buf);

  /// insertAllMethodsInVM - Insert all methods defined by this class loader
  /// in the VM.
  ///
  void insertAllMethodsInVM(Jnjvm* vm);

  /// loadLibFromJar - Try to load the shared library compiled by vmjc with
  /// this jar file.
  ///
  void loadLibFromJar(Jnjvm* vm, const char* name, const char* file);

  /// loadLibFromFile - Try to load the shared library compiled by vmjc with
  /// this class file.
  ///
  void loadLibFromFile(Jnjvm* vm, const char* name);
  
  /// loadClassFromSelf - Load the main class if we are an executable.
  ///
  Class* loadClassFromSelf(Jnjvm* vm, const char* name);

};

/// JnjvmBootstrapLoader - This class is for the bootstrap class loader, which
/// loads base classes, ie glibj.zip or rt.jar and -Xbootclasspath.
///
class JnjvmBootstrapLoader : public JnjvmClassLoader {
private:
  /// internalLoad - Load the class with the given name.
  ///
  virtual UserClass* internalLoad(const UTF8* utf8, bool doResolve,
                                  JavaString* strName = 0);
     
  /// bootClasspath - List of paths for the base classes.
  ///
  std::vector<const char*> bootClasspath;

  /// bootArchives - List of .zip or .jar files that contain base classes.
  ///
  std::vector<ZipArchive*> bootArchives;
  
  /// openName - Opens a file of the given name and returns it as an array
  /// of byte.
  ///
  ArrayUInt8* openName(const UTF8* utf8);
  
public:
  
  /// tracer - Traces instances of this class.
  ///
  virtual void tracer();

  /// libClasspathEnv - The paths for dynamic libraries of Classpath, separated
  /// by ':'.
  ///
  const char* libClasspathEnv;

  /// bootClasspathEnv - The path for base classes, seperated by '.'.
  ///
  const char* bootClasspathEnv;

  /// analyseClasspathEnv - Analyse the paths for base classes.
  ///
  void analyseClasspathEnv(const char*);
  
  /// createBootstrapLoader - Creates the bootstrap loader, first thing
  /// to do before any execution of a JVM. Also try to load libvmjc.so
  /// if dlLoad is not false.
  ///
  JnjvmBootstrapLoader(mvm::BumpPtrAllocator& Alloc, JavaCompiler* Comp,
                       bool dlLoad = true);
  
  virtual JavaString* UTF8ToStr(const UTF8* utf8);

  /// nativeHandle - Non-null handle if boot classes were static compiled in
  /// a dynamic library
  ///
  void* nativeHandle;

  /// upcalls - Upcall classes, fields and methods so that C++ code can call
  /// Java code.
  ///
  Classpath* upcalls;
  
  /// InterfacesArray - The interfaces that array classes implement.
  ///
  UserClass** InterfacesArray;

  /// SuperArray - The super of array classes.
  UserClass* SuperArray;

  /// Lists of UTF8s used internaly in VMKit.
  const UTF8* NoClassDefFoundError;
  const UTF8* initName;
  const UTF8* clinitName;
  const UTF8* clinitType; 
  const UTF8* initExceptionSig;
  const UTF8* runName; 
  const UTF8* prelib; 
  const UTF8* postlib; 
  const UTF8* mathName;
  const UTF8* stackWalkerName;
  const UTF8* abs;
  const UTF8* sqrt;
  const UTF8* sin;
  const UTF8* cos;
  const UTF8* tan;
  const UTF8* asin;
  const UTF8* acos;
  const UTF8* atan;
  const UTF8* atan2;
  const UTF8* exp;
  const UTF8* log;
  const UTF8* pow;
  const UTF8* ceil;
  const UTF8* floor;
  const UTF8* rint;
  const UTF8* cbrt;
  const UTF8* cosh;
  const UTF8* expm1;
  const UTF8* hypot;
  const UTF8* log10;
  const UTF8* log1p;
  const UTF8* sinh;
  const UTF8* tanh;
  const UTF8* finalize;

  /// primitiveMap - Map of primitive classes, hashed by id.
  std::map<const char, UserClassPrimitive*> primitiveMap;

  UserClassPrimitive* getPrimitiveClass(char id) {
    return primitiveMap[id];
  }

  /// arrayTable - Table of array classes.
  UserClassArray* arrayTable[8];

  UserClassArray* getArrayClass(unsigned id) {
    return arrayTable[id - 4];
  }

  virtual ~JnjvmBootstrapLoader();
};

/// VMClassLoader - The vmdata object that will be placed in and will only
/// be referenced by the java.lang.Classloader Java object. Having a
/// separate class between VMClassLoader and JnjvmClassLoader allows to
/// have a JnjvmClassLoader non-GC object. Also the finalizer of this class
/// will delete the internal class loader and we do not have to implement
/// hacks in the java.lang.Classloader finalizer.
class VMClassLoader : public gc {
private:
  
  /// JCL - The internal class loader.
  ///
  JnjvmClassLoader* JCL;

public:

  static VMClassLoader* allocate(JnjvmClassLoader* J) {
    VMClassLoader* res = 
      (VMClassLoader*)gc::operator new(sizeof(VMClassLoader), &VT);
    res->JCL = J;
    return res;
  }

  /// VT - The VirtualTable for this GC-class.
  static VirtualTable VT;

  /// TRACER - Trace the internal class loader.
  static void staticTracer(VMClassLoader* obj) {
    if (obj->JCL) obj->JCL->tracer();
  }

  /// ~VMClassLoader - Delete the internal class loader.
  ///
  static void staticDestructor(VMClassLoader* obj) {
    if (obj->JCL) obj->JCL->~JnjvmClassLoader();
  }

  /// VMClassLoader - Default constructors.
  ///
  VMClassLoader(JnjvmClassLoader* J) : JCL(J) {}

  /// getClassLoader - Get the internal class loader.
  ///
  JnjvmClassLoader* getClassLoader() {
    return JCL;
  }

};

} // end namespace jnjvm

#endif // JNJVM_CLASSLOADER_H
