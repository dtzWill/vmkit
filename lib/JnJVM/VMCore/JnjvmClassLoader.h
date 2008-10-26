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

#include <vector>

#include "types.h"

#include "mvm/Allocator.h"
#include "mvm/Object.h"
#include "mvm/PrintBuffer.h"

#include "JnjvmConfig.h"

namespace jnjvm {

class ArrayUInt8;
class Attribut;
class UserClass;
class UserClassArray;
class ClassMap;
class Classpath;
class UserCommonClass;
class JavaObject;
class JavaString;
class Jnjvm;
class JnjvmBootstrapLoader;
class JnjvmModule;
class JnjvmModuleProvider;
class Reader;
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
class JnjvmClassLoader : public mvm::Object {
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
  virtual UserClass* internalLoad(const UTF8* utf8);
  
  /// internalConstructType - Hashes a Typedef, an internal representation of
  /// a class still not loaded.
  ///
  Typedef* internalConstructType(const UTF8 * name);
  
  /// JnjvmClassLoader - Allocate a user-defined class loader. Called on
  /// first use of a Java class loader.
  ///
  JnjvmClassLoader(JnjvmClassLoader& JCL, JavaObject* loader, Jnjvm* isolate);

protected:

  /// classes - The classes this class loader has loaded.
  ///
  ClassMap* classes;
  
  /// javaTypes - Tables of Typedef defined by this class loader. Shared by all
  /// class loaders in a no isolation configuration.
  ///
  TypeMap* javaTypes;

  /// javaSignatures - Tables of Signdef defined by this class loader. Shared
  /// by all class loaders in a no isolation configuration.
  ///
  SignMap* javaSignatures;

public:
  
  /// VT - The virtual table of this class.
  ///
  static VirtualTable* VT;
  
  /// allocator - Reference to the memory allocator, which will allocate UTF8s,
  /// signatures and types.
  ///
  mvm::BumpPtrAllocator allocator;
   
  
  /// hashUTF8 - Tables of UTF8s defined by this class loader. Shared
  /// by all class loaders in a no isolation configuration.
  ///
  UTF8Map * hashUTF8;
  
  /// TheModule - JIT module for compiling methods.
  ///
  JnjvmModule* TheModule;

  /// TheModuleProvider - JIT module provider for dynamic class loading and
  /// lazy compilation.
  ///
  JnjvmModuleProvider* TheModuleProvider;

  /// tracer - Traces a JnjvmClassLoader for GC.
  ///
  virtual void TRACER;
  
  /// print - String representation of the loader for debugging purposes.
  ///
  virtual void print(mvm::PrintBuffer* buf) const {
    buf->write("Java class loader<>");
  } 
  
  /// getJnjvmLoaderFromJavaObject - Return the Jnjvm runtime representation
  /// of the given class loader.
  ///
  static JnjvmClassLoader* getJnjvmLoaderFromJavaObject(JavaObject*, Jnjvm *vm);
  
  /// getJavaClassLoader - Return the Java representation of this class loader.
  ///
  JavaObject* getJavaClassLoader() {
    return javaLoader;
  }
  
  /// loadName - Loads the class of the given name.
  ///
  UserClass* loadName(const UTF8* name, bool doResolve, bool doThrow);
  
  /// lookupClassFromUTF8 - Lookup a class from an UTF8 name and load it.
  ///
  UserCommonClass* lookupClassFromUTF8(const UTF8* utf8, Jnjvm* vm,
                                       bool doResolve, bool doThrow);
  
  /// lookupClassFromJavaString - Lookup a class from a Java String and load it.
  ///
  UserCommonClass* lookupClassFromJavaString(JavaString* str, Jnjvm* vm, 
                                             bool doResolve, bool doThrow);
   
  /// lookupClass - Finds the class of th given name in the class loader's
  /// table.
  ///
  UserCommonClass* lookupClass(const UTF8* utf8);

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
  ISOLATE_STATIC JnjvmBootstrapLoader* bootstrapLoader;
  
  /// ~JnjvmClassLoader - Destroy the loader. Depending on the JVM
  /// configuration, this may destroy the tables, JIT module and
  /// module provider.
  ///
  ~JnjvmClassLoader();
  
  /// JnjvmClassLoader - Default constructor, zeroes the field.
  ///
  JnjvmClassLoader() {
    hashUTF8 = 0;
    javaTypes = 0;
    javaSignatures = 0;
    TheModule = 0;
    TheModuleProvider = 0;
    isolate = 0;
    classes = 0;
  }

  UserClass* loadClass;
  
  const UTF8* constructArrayName(uint32 steps, const UTF8* className);
  
  virtual JavaString* UTF8ToStr(const UTF8* utf8);

  /// Strings hashed by this classloader.
  std::vector<JavaString*, gc_allocator<JavaString*> > strings;
  
  /// nativeLibs - Native libraries (e.g. '.so') loaded by this class loader.
  ///
  std::vector<void*> nativeLibs;

  void* loadLib(const char* buf, bool& jnjvm);
};

/// JnjvmBootstrapLoader - This class is for the bootstrap class loader, which
/// loads base classes, ie glibj.zip or rt.jar and -Xbootclasspath.
///
class JnjvmBootstrapLoader : public JnjvmClassLoader {
private:
  /// internalLoad - Load the class with the given name.
  ///
  virtual UserClass* internalLoad(const UTF8* utf8);
     
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
  
  /// VT - The virtual table of this class.
  ///
  static VirtualTable* VT;
  
  /// tracer - Traces instances of this class.
  ///
  virtual void TRACER;

  /// print - String representation of the loader, for debugging purposes.
  ///
  virtual void print(mvm::PrintBuffer* buf) const {
    buf->write("Jnjvm bootstrap loader<>");
  } 
  
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
  /// to do before any execution of a JVM.
  ///
  JnjvmBootstrapLoader(uint32 memLimit);
  JnjvmBootstrapLoader() {}
  
  virtual JavaString* UTF8ToStr(const UTF8* utf8);


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
};

} // end namespace jnjvm

#endif // JNJVM_CLASSLOADER_H
