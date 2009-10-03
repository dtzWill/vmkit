//===-------- JavaClass.h - Java class representation -------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef JNJVM_JAVA_CLASS_H
#define JNJVM_JAVA_CLASS_H


#include "types.h"

#include "mvm/Allocator.h"
#include "mvm/Object.h"
#include "mvm/Threads/Cond.h"
#include "mvm/Threads/Locks.h"

#include "JavaAccess.h"
#include "JavaArray.h"
#include "JnjvmClassLoader.h"
#include "JnjvmConfig.h"

#include <cassert>

namespace jnjvm {

class ArrayObject;
class ArrayUInt8;
class ArrayUInt16;
class Enveloppe;
class Class;
class ClassArray;
class JavaArray;
class JavaConstantPool;
class JavaField;
class JavaMethod;
class JavaObject;
class JavaVirtualTable;
class Reader;
class Signdef;
class Typedef;


/// JavaState - List of states a Java class can have. A class is ready to be
/// used (i.e allocating instances of the class, calling methods of the class
/// and accessing static fields of the class) when it is in the ready state.
///
#define loaded 0       /// The .class file has been found.
#define classRead 1    /// The .class file has been read.
#define resolved 2     /// The class has been resolved.
#define vmjc 3         /// The class is defined in a shared library.
#define inClinit 4     /// The class is cliniting.
#define ready 5        /// The class is ready to be used.
#define erroneous 6    /// The class is in an erroneous state.


/// Attribut - This class represents JVM attributes to Java class, methods and
/// fields located in the .class file.
///
class Attribut : public mvm::PermanentObject {
public:
  
  /// name - The name of the attribut. These are specified in the JVM book.
  /// Experimental attributes exist, but the JnJVM does nor parse them.
  ///
  const UTF8* name;

  /// start - The offset in the class of this attribut.
  ///
  uint32 start;

  /// nbb - The size of the attribut.
  ///
  uint32 nbb;

  /// Attribut - Create an attribut at the given length and offset.
  ///
  Attribut(const UTF8* name, uint32 length, uint32 offset);
  Attribut() {}

  /// codeAttribut - The "Code" JVM attribut. This is a method attribut for
  /// finding the bytecode of a method in the .class file.
  //
  static const UTF8* codeAttribut;

  /// exceptionsAttribut - The "Exceptions" attribut. This is a method
  /// attribut for finding the exception table of a method in the .class
  /// file.
  ///
  static const UTF8* exceptionsAttribut;

  /// constantAttribut - The "ConstantValue" attribut. This is a field attribut
  /// when the field has a static constant value.
  ///
  static const UTF8* constantAttribut;

  /// lineNumberTableAttribut - The "LineNumberTable" attribut. This is used
  /// for corresponding JVM bytecode to source line in the .java file.
  ///
  static const UTF8* lineNumberTableAttribut;

  /// innerClassAttribut - The "InnerClasses" attribut. This is a class attribut
  /// for knowing the inner/outer informations of a Java class.
  ///
  static const UTF8* innerClassesAttribut;

  /// sourceFileAttribut - The "SourceFile" attribut. This is a class attribut
  /// and gives the correspondance between a class and the name of its Java
  /// file.
  ///
  static const UTF8* sourceFileAttribut;
  
};

/// TaskClassMirror - The isolate specific class information: the initialization
/// state and the static instance. In a non-isolate environment, there is only
/// one instance of a TaskClassMirror per Class.
class TaskClassMirror {
public:
  
  /// status - The state.
  ///
  uint8 status;

  /// initialized - Is the class initialized?
  bool initialized;

  /// staticInstance - Memory that holds the static variables of the class.
  ///
  void* staticInstance;
};

/// CommonClass - This class is the root class of all Java classes. It is
/// GC-allocated because CommonClasses have to be traceable. A java/lang/Class
/// object that stays in memory has a reference to the class. Same for
/// super or interfaces.
///
class CommonClass : public mvm::PermanentObject {
#ifdef ISOLATE_SHARING
friend class UserCommonClass;
#endif

public:
  
//===----------------------------------------------------------------------===//
//
// If you want to add new fields or modify the types of fields, you must also
// change their LLVM representation in LLVMRuntime/runtime-*.ll, and their
// offsets in JnjvmModule.cpp.
//
//===----------------------------------------------------------------------===//
 
  /// delegatees - The java/lang/Class delegatee.
  ///
  JavaObject* delegatee[NR_ISOLATES];

  /// access - {public, private, protected}.
  ///
  uint16 access;
  
  /// interfaces - The interfaces this class implements.
  ///
  Class** interfaces; 
  uint16 nbInterfaces;
  
  /// name - The name of the class.
  ///
  const UTF8* name;
   
  /// super - The parent of this class.
  ///
  Class * super;
   
  /// classLoader - The Jnjvm class loader that loaded the class.
  ///
  JnjvmClassLoader* classLoader;
  
  /// virtualVT - The virtual table of instances of this class.
  ///
  JavaVirtualTable* virtualVT;
  

//===----------------------------------------------------------------------===//
//
// End field declaration.
//
//===----------------------------------------------------------------------===//

  bool isSecondaryClass() {
    return virtualVT->offset == JavaVirtualTable::getCacheIndex();
  }

  // Assessor methods.
  uint32 getAccess() const      { return access; }
  Class** getInterfaces() const { return interfaces; }
  const UTF8* getName() const   { return name; }
  Class* getSuper() const       { return super; }
  
  /// isArray - Is the class an array class?
  ///
  bool isArray() const {
    return jnjvm::isArray(access);
  }
  
  /// isPrimitive - Is the class a primitive class?
  ///
  bool isPrimitive() const {
    return jnjvm::isPrimitive(access);
  }
  
  /// isInterface - Is the class an interface?
  ///
  bool isInterface() const {
    return jnjvm::isInterface(access);
  }
  
  /// isClass - Is the class a real, instantiable class?
  ///
  bool isClass() const {
    return jnjvm::isClass(access);
  }

  /// asClass - Returns the class as a user-defined class
  /// if it is not a primitive or an array.
  ///
  UserClass* asClass() {
    if (isClass()) return (UserClass*)this;
    return 0;
  }
  
  /// asClass - Returns the class as a user-defined class
  /// if it is not a primitive or an array.
  ///
  const UserClass* asClass() const {
    if (isClass()) return (const UserClass*)this;
    return 0;
  }
  
  /// asPrimitiveClass - Returns the class if it's a primitive class.
  ///
  UserClassPrimitive* asPrimitiveClass() {
    if (isPrimitive()) return (UserClassPrimitive*)this;
    return 0;
  }
  
  const UserClassPrimitive* asPrimitiveClass() const {
    if (isPrimitive()) return (const UserClassPrimitive*)this;
    return 0;
  }
  
  /// asArrayClass - Returns the class if it's an array class.
  ///
  UserClassArray* asArrayClass() {
    if (isArray()) return (UserClassArray*)this;
    return 0;
  }
  
  const UserClassArray* asArrayClass() const {
    if (isArray()) return (const UserClassArray*)this;
    return 0;
  }

  /// tracer - The tracer of this GC-allocated class.
  ///
  void tracer();
  
  /// inheritName - Does this class in its class hierarchy inherits
  /// the given name? Equality is on the name. This function does not take
  /// into account array classes.
  ///
  bool inheritName(const uint16* buf, uint32 len);

  /// isOfTypeName - Does this class inherits the given name? Equality is on
  /// the name. This function takes into account array classes.
  ///
  bool isOfTypeName(const UTF8* Tname);

  /// isAssignableFrom - Is this class assignable from the given class? The
  /// classes may be of any type.
  ///
  bool isAssignableFrom(CommonClass* cl);

  /// getClassDelegatee - Return the java/lang/Class representation of this
  /// class.
  ///
  JavaObject* getClassDelegatee(Jnjvm* vm, JavaObject* pd = 0);
  
  /// getClassDelegateePtr - Return a pointer on the java/lang/Class
  /// representation of this class. Used for JNI.
  ///
  JavaObject* const* getClassDelegateePtr(Jnjvm* vm, JavaObject* pd = 0);
  
  /// CommonClass - Create a class with th given name.
  ///
  CommonClass(JnjvmClassLoader* loader, const UTF8* name);
  
  /// ~CommonClass - Free memory used by this class, and remove it from
  /// metadata.
  ///
  ~CommonClass();

  /// setInterfaces - Set the interfaces of the class.
  ///
  void setInterfaces(Class** I) {
    interfaces = I;
  }
 
  /// toPrimitive - Returns the primitive class which represents
  /// this class, ie void for java/lang/Void.
  ///
  UserClassPrimitive* toPrimitive(Jnjvm* vm) const;
 
  /// getInternal - Return the class.
  ///
  CommonClass* getInternal() {
    return this;
  }
 
  /// setDelegatee - Set the java/lang/Class object of this class.
  ///
  JavaObject* setDelegatee(JavaObject* val);

#if !defined(ISOLATE) && !defined(ISOLATE_SHARING)
  /// getDelegatee - Get the java/lang/Class object representing this class.
  ///
  JavaObject* getDelegatee() const {
    return delegatee[0];
  }
  
  /// getDelegatee - Get a pointer on the java/lang/Class object
  /// representing this class.
  ///
  JavaObject* const* getDelegateePtr() const {
    return delegatee;
  }

#else
#if defined(ISOLATE)
  JavaObject* getDelegatee();
  JavaObject** getDelegateePtr();
#endif
#endif
  
  /// resolvedImplClass - Return the internal representation of the
  /// java.lang.Class object. The class must be resolved.
  //
  static UserCommonClass* resolvedImplClass(Jnjvm* vm, JavaObject* delegatee,
                                            bool doClinit);
#ifdef USE_GC_BOEHM
  void* operator new(size_t sz, mvm::BumpPtrAllocator& allocator) {
    return GC_MALLOC(sz);
  }
#endif

};

/// ClassPrimitive - This class represents internal classes for primitive
/// types, e.g. java/lang/Integer.TYPE.
///
class ClassPrimitive : public CommonClass {
public:
  
  /// logSize - The log size of this class, eg 2 for int.
  ///
  uint32 logSize;
  
  
  /// ClassPrimitive - Constructs a primitive class. Only called at boot
  /// time.
  ///
  ClassPrimitive(JnjvmClassLoader* loader, const UTF8* name, uint32 nb);

  /// byteIdToPrimitive - Get the primitive class from its byte representation,
  /// ie int for I.
  ///
  static UserClassPrimitive* byteIdToPrimitive(char id, Classpath* upcalls);
  
};


/// Class - This class is the representation of Java regular classes (i.e not
/// array or primitive). Theses classes have a constant pool.
///
class Class : public CommonClass {

private:

  /// FatLock - This class is the inflated lock of Class instances. It should
  /// be very rare that such locks are allocated.
  class FatLock : public mvm::PermanentObject {
  public:
    /// lockVar - When multiple threads want to load/resolve/initialize a class,
    /// they must be synchronized so that these steps are only performed once
    /// for a given class.
    mvm::LockRecursive lockVar;

    /// condVar - Used to wake threads waiting on the load/resolve/initialize
    /// process of this class, done by another thread.
    mvm::Cond condVar;
  
    bool owner() {
      return lockVar.selfOwner();
    }

    mvm::Thread* getOwner() {
      return lockVar.getOwner();
    }

    static FatLock* allocate(UserCommonClass* cl) {
      return new(cl->classLoader->allocator, "Class fat lock") FatLock();
    }

    void acquire() {
      lockVar.lock();
    }

    void acquireAll(uint32 nb) {
      lockVar.lockAll(nb);
    }

    void release() {
      lockVar.unlock();
    }

    void broadcast() {
      condVar.broadcast();
    }

    void wait() {
      condVar.wait(&lockVar);
    }
  };

public:
  
  /// virtualSize - The size of instances of this class.
  /// 
  uint32 virtualSize;

  /// IsolateInfo - Per isolate informations for static instances and
  /// initialization state.
  ///
  TaskClassMirror IsolateInfo[NR_ISOLATES];
   
  /// lock - The lock of this class. It should be very rare that this lock
  /// inflates.
  ///
  mvm::ThinLock<FatLock, CommonClass> lock;
  
  /// virtualFields - List of all the virtual fields defined in this class.
  /// This does not contain non-redefined super fields.
  JavaField* virtualFields;
  
  /// nbVirtualFields - The number of virtual fields.
  ///
  uint16 nbVirtualFields;

  /// staticFields - List of all the static fields defined in this class.
  ///
  JavaField* staticFields;

  /// nbStaticFields - The number of static fields.
  ///
  uint16 nbStaticFields;
  
  /// virtualMethods - List of all the virtual methods defined by this class.
  /// This does not contain non-redefined super methods.
  JavaMethod* virtualMethods;

  /// nbVirtualMethods - The number of virtual methods.
  ///
  uint16 nbVirtualMethods;
  
  /// staticMethods - List of all the static methods defined by this class.
  ///
  JavaMethod* staticMethods;

  /// nbStaticMethods - The number of static methods.
  ///
  uint16 nbStaticMethods;
  
  /// ownerClass - Who is initializing this class.
  ///
  mvm::Thread* ownerClass;
  
  /// bytes - The .class file of this class.
  ///
  ArrayUInt8* bytes;

  /// ctpInfo - The constant pool info of this class.
  ///
  JavaConstantPool* ctpInfo;

  /// attributs - JVM attributes of this class.
  ///
  Attribut* attributs;

  /// nbAttributs - The number of attributes.
  ///
  uint16 nbAttributs;
  
  /// innerClasses - The inner classes of this class.
  ///
  Class** innerClasses;
  
  /// nbInnerClasses - The number of inner classes.
  ///
  uint16 nbInnerClasses;

  /// outerClass - The outer class, if this class is an inner class.
  ///
  Class* outerClass;
  
  /// innerAccess - The access of this class, if this class is an inner class.
  ///
  uint16 innerAccess;

  /// innerOuterResolved - Is the inner/outer resolution done?
  ///
  bool innerOuterResolved;
  
  /// isAnonymous - Is the class an anonymous class?
  ///
  bool isAnonymous;

  /// virtualTableSize - The size of the virtual table of this class.
  ///
  uint32 virtualTableSize;
  
  /// staticSize - The size of the static instance of this class.
  ///
  uint32 staticSize;
  
  /// JInfo - JIT specific information.
  ///
  mvm::JITInfo* JInfo;
  
  /// getVirtualSize - Get the virtual size of instances of this class.
  ///
  uint32 getVirtualSize() const { return virtualSize; }
  
  /// getVirtualVT - Get the virtual VT of instances of this class.
  ///
  JavaVirtualTable* getVirtualVT() const { return virtualVT; }

  /// getOwnerClass - Get the thread that is currently initializing the class.
  ///
  mvm::Thread* getOwnerClass() const {
    return ownerClass;
  }

  /// setOwnerClass - Set the thread that is currently initializing the class.
  ///
  void setOwnerClass(mvm::Thread* th) {
    ownerClass = th;
  }
 
  /// getOuterClass - Get the class that contains the definition of this class.
  ///
  Class* getOuterClass() const {
    return outerClass;
  }

  /// getInnterClasses - Get the classes that this class defines.
  ///
  Class** getInnerClasses() const {
    return innerClasses;
  }

  /// lookupMethodDontThrow - Lookup a method in the method map of this class.
  /// Do not throw if the method is not found.
  ///
  JavaMethod* lookupMethodDontThrow(const UTF8* name, const UTF8* type,
                                    bool isStatic, bool recurse, Class** cl);
  
  /// lookupInterfaceMethodDontThrow - Lookup a method in the interfaces of
  /// this class.
  /// Do not throw if the method is not found.
  ///
  JavaMethod* lookupInterfaceMethodDontThrow(const UTF8* name,
                                             const UTF8* type);
  
  /// lookupMethod - Lookup a method and throw an exception if not found.
  ///
  JavaMethod* lookupMethod(const UTF8* name, const UTF8* type, bool isStatic,
                           bool recurse, Class** cl);
  
  /// lookupFieldDontThrow - Lookup a field in the field map of this class. Do
  /// not throw if the field is not found.
  ///
  JavaField* lookupFieldDontThrow(const UTF8* name, const UTF8* type,
                                  bool isStatic, bool recurse,
                                  Class** definingClass);
  
  /// lookupField - Lookup a field and throw an exception if not found.
  ///
  JavaField* lookupField(const UTF8* name, const UTF8* type, bool isStatic,
                         bool recurse, Class** definingClass);
   
  /// Assessor methods.
  JavaField* getStaticFields() const    { return staticFields; }
  JavaField* getVirtualFields() const   { return virtualFields; }
  JavaMethod* getStaticMethods() const  { return staticMethods; }
  JavaMethod* getVirtualMethods() const { return virtualMethods; }

  
  /// setInnerAccess - Set the access flags of this inner class.
  ///
  void setInnerAccess(uint16 access) {
    innerAccess = access;
  }
   
  /// getStaticSize - Get the size of the static instance.
  ///
  uint32 getStaticSize() const {
    return staticSize;
  }
  
#ifndef ISOLATE_SHARING
  /// doNew - Allocates a Java object whose class is this class.
  ///
  JavaObject* doNew(Jnjvm* vm);
#endif
  
  /// tracer - Tracer function of instances of Class.
  ///
  void tracer();
  
  ~Class();
  Class();
  
  /// lookupAttribut - Look up a JVM attribut of this class.
  ///
  Attribut* lookupAttribut(const UTF8* key);
  
  /// allocateStaticInstance - Allocate the static instance of this class.
  ///
  void* allocateStaticInstance(Jnjvm* vm);
  
  /// Class - Create a class in the given virtual machine and with the given
  /// name.
  Class(JnjvmClassLoader* loader, const UTF8* name, ArrayUInt8* bytes);
  
  /// readParents - Reads the parents, i.e. super and interfaces, of the class.
  ///
  void readParents(Reader& reader);

  /// loadParents - Loads and resolves the parents, i.e. super and interfarces,
  /// of the class.
  ///
  void loadParents();
  
  /// loadExceptions - Loads and resolves the exception classes used in catch 
  /// clauses of methods defined in this class.
  ///
  void loadExceptions();

  /// readAttributs - Reads the attributs of the class.
  ///
  Attribut* readAttributs(Reader& reader, uint16& size);

  /// readFields - Reads the fields of the class.
  ///
  void readFields(Reader& reader);

  /// readMethods - Reads the methods of the class.
  ///
  void readMethods(Reader& reader);
  
  /// readClass - Reads the class.
  ///
  void readClass();
 
  /// getConstantPool - Get the constant pool of the class.
  ///
  JavaConstantPool* getConstantPool() const {
    return ctpInfo;
  }

  /// getBytes - Get the bytes of the class file.
  ///
  ArrayUInt8* getBytes() const {
    return bytes;
  }
  
  ArrayUInt8** getBytesPtr() {
    return &bytes;
  }
  
  /// resolveInnerOuterClasses - Resolve the inner/outer information.
  ///
  void resolveInnerOuterClasses();

  /// getInfo - Get the JIT specific information, allocating one if it
  /// does not exist.
  ///
  template<typename Ty> 
  Ty *getInfo() {
    if (!JInfo) {
      JInfo = new(classLoader->allocator, "Class JIT info") Ty(this);
    }   

    assert((void*)dynamic_cast<Ty*>(JInfo) == (void*)JInfo &&
           "Invalid concrete type or multiple inheritence for getInfo");
    return static_cast<Ty*>(JInfo);
  }
  
  void clearInfo() {
    if (JInfo) JInfo->clear();
  }
  
  /// resolveClass - If the class has not been resolved yet, resolve it.
  ///
  void resolveClass();

  /// initialiseClass - If the class has not been initialized yet,
  /// initialize it.
  ///
  void initialiseClass(Jnjvm* vm);
  
  /// acquire - Acquire this class lock.
  ///
  void acquire() {
    lock.acquire(this);
  }
  
  /// release - Release this class lock.
  ///
  void release() {
    lock.release();  
  }

  /// waitClass - Wait for the class to be loaded/initialized/resolved.
  ///
  void waitClass() {
    FatLock* FL = lock.changeToFatlock(this);
    FL->wait();
  }
  
  /// broadcastClass - Unblock threads that were waiting on the class being
  /// loaded/initialized/resolved.
  ///
  void broadcastClass() {
    lock.broadcast();  
  }
  
#ifndef ISOLATE
  
  /// getCurrentTaskClassMirror - Get the class task mirror of the executing
  /// isolate.
  ///
  TaskClassMirror& getCurrentTaskClassMirror() {
    return IsolateInfo[0];
  }
  
  /// isReadyForCompilation - Can this class be inlined when JITing?
  ///
  bool isReadyForCompilation() {
    return isReady();
  }
  
  /// setResolved - Set the status of the class as resolved.
  ///
  void setResolved() {
    getCurrentTaskClassMirror().status = resolved;
  }
  
  /// setErroneous - Set the class as erroneous.
  ///
  void setErroneous() {
    getCurrentTaskClassMirror().status = erroneous;
  }
  
  /// setIsRead - The class file has been read.
  ///
  void setIsRead() {
    getCurrentTaskClassMirror().status = classRead;
  }
  

#else
  
  TaskClassMirror& getCurrentTaskClassMirror();
  
  bool isReadyForCompilation() {
    return false;
  }
  
  void setResolved() {
    for (uint32 i = 0; i < NR_ISOLATES; ++i) {
      IsolateInfo[i].status = resolved;
    }
  }
  
  void setIsRead() {
    for (uint32 i = 0; i < NR_ISOLATES; ++i) {
      IsolateInfo[i].status = classRead;
    }
  }
  
  void setErroneous() {
    for (uint32 i = 0; i < NR_ISOLATES; ++i) {
      IsolateInfo[i].status = erroneous;
    }
  }

#endif
  
  /// getStaticInstance - Get the memory that holds static variables.
  ///
  void* getStaticInstance() {
    return getCurrentTaskClassMirror().staticInstance;
  }
  
  /// setStaticInstance - Set the memory that holds static variables.
  ///
  void setStaticInstance(void* val) {
    getCurrentTaskClassMirror().staticInstance = val;
  }
  
  /// getInitializationState - Get the state of the class.
  ///
  uint8 getInitializationState() {
    return getCurrentTaskClassMirror().status;
  }

  /// setInitializationState - Set the state of the class.
  ///
  void setInitializationState(uint8 st) {
    TaskClassMirror& TCM = getCurrentTaskClassMirror();
    TCM.status = st;
    if (st == ready) TCM.initialized = true;
  }
  
  /// isReady - Has this class been initialized?
  ///
  bool isReady() {
    return getCurrentTaskClassMirror().status == ready;
  }
  
  /// isInitializing - Is the class currently being initialized?
  ///
  bool isInitializing() {
    return getCurrentTaskClassMirror().status >= inClinit;
  }
  
  /// isResolved - Has this class been resolved?
  ///
  bool isResolved() {
    uint8 stat = getCurrentTaskClassMirror().status;
    return (stat >= resolved && stat != erroneous);
  }
  
  /// isErroneous - Is the class in an erroneous state.
  ///
  bool isErroneous() {
    return getCurrentTaskClassMirror().status == erroneous;
  }

  /// isResolving - Is the class currently being resolved?
  ///
  bool isResolving() {
    return getCurrentTaskClassMirror().status == classRead;
  }

  /// isClassRead - Has the .class file been read?
  ///
  bool isClassRead() {
    return getCurrentTaskClassMirror().status >= classRead;
  }
 
  /// isNativeOverloaded - Is the method overloaded with a native function?
  ///
  bool isNativeOverloaded(JavaMethod* meth);

  /// needsInitialisationCheck - Does the method need an initialisation check?
  ///
  bool needsInitialisationCheck();

private:

  /// makeVT - Create the virtual table of this class.
  ///
  void makeVT();

};

/// ClassArray - This class represents Java array classes.
///
class ClassArray : public CommonClass {

public:
  
  /// doNew - Allocate a new array with the given allocator.
  ///
  JavaArray* doNew(sint32 n, mvm::BumpPtrAllocator& allocator,
                   bool temp = false);
  JavaArray* doNew(sint32 n, mvm::Allocator& allocator);

  /// _baseClass - The base class of the array.
  ///
  CommonClass*  _baseClass;

  /// baseClass - Get the base class of this array class.
  ///
  CommonClass* baseClass() const {
    return _baseClass;
  }

  /// doNew - Allocate a new array in the given vm.
  ///
  JavaArray* doNew(sint32 n, Jnjvm* vm);

  /// ClassArray - Construct a Java array class with the given name.
  ///
  ClassArray(JnjvmClassLoader* loader, const UTF8* name,
             UserCommonClass* baseClass);
  
  /// SuperArray - The super of class arrays. Namely java/lang/Object.
  ///
  static Class* SuperArray;

  /// InterfacesArray - The list of interfaces for array classes.
  ///
  static Class** InterfacesArray;

  /// initialiseVT - Initialise the primitive and reference array VT.
  /// super is the java/lang/Object class.
  ///
  static void initialiseVT(Class* javaLangObject);
  
};

/// JavaMethod - This class represents Java methods.
///
class JavaMethod : public mvm::PermanentObject {
private:

  /// _signature - The signature of this method. Null if not resolved.
  ///
  Signdef* _signature;

public:
  
  /// constructMethod - Create a new method.
  ///
  void initialise(Class* cl, const UTF8* name, const UTF8* type, uint16 access);
   
  /// compiledPtr - Return a pointer to the compiled code of this Java method,
  /// compiling it if necessary.
  ///
  void* compiledPtr();

  /// setCompiledPtr - Set the pointer function to the method.
  ///
  void setCompiledPtr(void*, const char*);
  
  /// JavaMethod - Delete the method as well as the cache enveloppes and
  /// attributes of the method.
  ///
  ~JavaMethod();

  /// access - Java access type of this method (e.g. private, public...).
  ///
  uint16 access;

  /// attributs - List of Java attributs of this method.
  ///
  Attribut* attributs;
  
  /// nbAttributs - The number of attributes.
  ///
  uint16 nbAttributs;

  /// enveloppes - List of caches in this method. For all invokeinterface
  /// bytecode there is a corresponding cache.
  ///
  Enveloppe* enveloppes;

  /// nbEnveloppes - The number of enveloppes.
  ///
  uint16 nbEnveloppes;
  
  /// classDef - The Java class where the method is defined.
  ///
  Class* classDef;

  /// name - The name of the method.
  ///
  const UTF8* name;

  /// type - The UTF8 signature of the method.
  ///
  const UTF8* type;

  /// canBeInlined - Can the method be inlined?
  ///
  bool canBeInlined;

  /// code - Pointer to the compiled code of this method.
  ///
  void* code;
  
  /// offset - The index of the method in the virtual table.
  ///
  uint32 offset;

  /// lookupAttribut - Look up an attribut in the method's attributs. Returns
  /// null if the attribut is not found.
  ///
  Attribut* lookupAttribut(const UTF8* key);

  /// getSignature - Get the signature of thes method, resolving it if
  /// necessary.
  ///
  Signdef* getSignature() {
    if(!_signature)
      _signature = classDef->classLoader->constructSign(type);
    return _signature;
  }
  
  /// toString - Return an array of chars, suitable for creating a string.
  ///
  ArrayUInt16* toString() const;
  
  /// jniConsFromMeth - Construct the JNI name of this method as if
  /// there is no other function in the class with the same name.
  ///
  void jniConsFromMeth(char* buf) const {
    jniConsFromMeth(buf, classDef->name, name, type, isSynthetic(access));
  }

  /// jniConsFromMethOverloaded - Construct the JNI name of this method
  /// as if its name is overloaded in the class.
  ///
  void jniConsFromMethOverloaded(char* buf) const {
    jniConsFromMethOverloaded(buf, classDef->name, name, type,
                              isSynthetic(access));
  }
  
  /// jniConsFromMeth - Construct the non-overloaded JNI name with
  /// the given name and type.
  ///
  static void jniConsFromMeth(char* buf, const UTF8* clName, const UTF8* name,
                              const UTF8* sign, bool synthetic);

  /// jniConsFromMethOverloaded - Construct the overloaded JNI name with
  /// the given name and type.
  ///
  static void jniConsFromMethOverloaded(char* buf, const UTF8* clName,
                                        const UTF8* name, const UTF8* sign,
                                        bool synthetic);
  
  /// getParameterTypes - Get the java.lang.Class of the parameters of
  /// the method, with the given class loader.
  ///
  ArrayObject* getParameterTypes(JnjvmClassLoader* loader);

  /// getExceptionTypes - Get the java.lang.Class of the exceptions of the
  /// method, with the given class loader.
  ///
  ArrayObject* getExceptionTypes(JnjvmClassLoader* loader);

  /// getReturnType - Get the java.lang.Class of the result of the method,
  /// with the given class loader.
  ///
  JavaObject* getReturnType(JnjvmClassLoader* loader);
  

//===----------------------------------------------------------------------===//
//
// Upcalls from JnJVM code to Java code. 
//
//===----------------------------------------------------------------------===//
  
  /// This class of methods takes a variable argument list.
  uint32 invokeIntSpecialAP(Jnjvm* vm, UserClass*, JavaObject* obj, va_list ap, bool jni = false)
    __attribute__ ((noinline));
  float invokeFloatSpecialAP(Jnjvm* vm, UserClass*, JavaObject* obj, va_list ap, bool jni = false)
    __attribute__ ((noinline));
  double invokeDoubleSpecialAP(Jnjvm* vm, UserClass*, JavaObject* obj,
                               va_list ap, bool jni = false) __attribute__ ((noinline));
  sint64 invokeLongSpecialAP(Jnjvm* vm, UserClass*, JavaObject* obj, va_list ap, bool jni = false)
    __attribute__ ((noinline));
  JavaObject* invokeJavaObjectSpecialAP(Jnjvm* vm, UserClass*, JavaObject* obj,
                                        va_list ap, bool jni = false) __attribute__ ((noinline));
  
  uint32 invokeIntVirtualAP(Jnjvm* vm, UserClass*, JavaObject* obj, va_list ap, bool jni = false)
    __attribute__ ((noinline));
  float invokeFloatVirtualAP(Jnjvm* vm, UserClass*, JavaObject* obj, va_list ap, bool jni = false)
    __attribute__ ((noinline));
  double invokeDoubleVirtualAP(Jnjvm* vm, UserClass*, JavaObject* obj,
                               va_list ap, bool jni = false) __attribute__ ((noinline));
  sint64 invokeLongVirtualAP(Jnjvm* vm, UserClass*, JavaObject* obj, va_list ap, bool jni = false)
    __attribute__ ((noinline));
  JavaObject* invokeJavaObjectVirtualAP(Jnjvm* vm, UserClass*, JavaObject* obj,
                                        va_list ap, bool jni = false) __attribute__ ((noinline));
  
  uint32 invokeIntStaticAP(Jnjvm* vm, UserClass*, va_list ap, bool jni = false)
    __attribute__ ((noinline));
  float invokeFloatStaticAP(Jnjvm* vm, UserClass*, va_list ap, bool jni = false)
    __attribute__ ((noinline));
  double invokeDoubleStaticAP(Jnjvm* vm, UserClass*, va_list ap, bool jni = false)
    __attribute__ ((noinline));
  sint64 invokeLongStaticAP(Jnjvm* vm, UserClass*, va_list ap, bool jni = false)
    __attribute__ ((noinline));
  JavaObject* invokeJavaObjectStaticAP(Jnjvm* vm, UserClass*, va_list ap, bool jni = false)
    __attribute__ ((noinline));

  /// This class of methods takes a buffer which contain the arguments of the
  /// call.
  uint32 invokeIntSpecialBuf(Jnjvm* vm, UserClass*, JavaObject* obj, void* buf)
    __attribute__ ((noinline));
  float invokeFloatSpecialBuf(Jnjvm* vm, UserClass*, JavaObject* obj, void* buf)
    __attribute__ ((noinline));
  double invokeDoubleSpecialBuf(Jnjvm* vm, UserClass*, JavaObject* obj,
                                void* buf) __attribute__ ((noinline));
  sint64 invokeLongSpecialBuf(Jnjvm* vm, UserClass*, JavaObject* obj, void* buf)
    __attribute__ ((noinline));
  JavaObject* invokeJavaObjectSpecialBuf(Jnjvm* vm, UserClass*, JavaObject* obj,
                                         void* buf) __attribute__ ((noinline));
  
  uint32 invokeIntVirtualBuf(Jnjvm* vm, UserClass*, JavaObject* obj, void* buf)
    __attribute__ ((noinline));
  float invokeFloatVirtualBuf(Jnjvm* vm, UserClass*, JavaObject* obj, void* buf)
    __attribute__ ((noinline));
  double invokeDoubleVirtualBuf(Jnjvm* vm, UserClass*, JavaObject* obj,
                                void* buf) __attribute__ ((noinline));
  sint64 invokeLongVirtualBuf(Jnjvm* vm, UserClass*, JavaObject* obj, void* buf)
    __attribute__ ((noinline));
  JavaObject* invokeJavaObjectVirtualBuf(Jnjvm* vm, UserClass*, JavaObject* obj,
                                         void* buf) __attribute__ ((noinline));
  
  uint32 invokeIntStaticBuf(Jnjvm* vm, UserClass*, void* buf)
    __attribute__ ((noinline));
  float invokeFloatStaticBuf(Jnjvm* vm, UserClass*, void* buf)
    __attribute__ ((noinline));
  double invokeDoubleStaticBuf(Jnjvm* vm, UserClass*, void* buf)
    __attribute__ ((noinline));
  sint64 invokeLongStaticBuf(Jnjvm* vm, UserClass*, void* buf)
    __attribute__ ((noinline));
  JavaObject* invokeJavaObjectStaticBuf(Jnjvm* vm, UserClass*, void* buf)
    __attribute__ ((noinline));

  /// This class of methods is variadic.
  uint32 invokeIntSpecial(Jnjvm* vm, UserClass*, JavaObject* obj, ...)
    __attribute__ ((noinline));
  float invokeFloatSpecial(Jnjvm* vm, UserClass*, JavaObject* obj, ...)
    __attribute__ ((noinline));
  double invokeDoubleSpecial(Jnjvm* vm, UserClass*, JavaObject* obj, ...)
    __attribute__ ((noinline));
  sint64 invokeLongSpecial(Jnjvm* vm, UserClass*, JavaObject* obj, ...)
    __attribute__ ((noinline));
  JavaObject* invokeJavaObjectSpecial(Jnjvm* vm, UserClass*, JavaObject* obj,
                                      ...) __attribute__ ((noinline));
  
  uint32 invokeIntVirtual(Jnjvm* vm, UserClass*, JavaObject* obj, ...)
    __attribute__ ((noinline));
  float invokeFloatVirtual(Jnjvm* vm, UserClass*, JavaObject* obj, ...)
    __attribute__ ((noinline));
  double invokeDoubleVirtual(Jnjvm* vm, UserClass*, JavaObject* obj, ...)
    __attribute__ ((noinline));
  sint64 invokeLongVirtual(Jnjvm* vm, UserClass*, JavaObject* obj, ...)
    __attribute__ ((noinline));
  JavaObject* invokeJavaObjectVirtual(Jnjvm* vm, UserClass*, JavaObject* obj,
                                      ...) __attribute__ ((noinline));
  
  uint32 invokeIntStatic(Jnjvm* vm, UserClass*, ...)
    __attribute__ ((noinline));
  float invokeFloatStatic(Jnjvm* vm, UserClass*, ...)
    __attribute__ ((noinline));
  double invokeDoubleStatic(Jnjvm* vm, UserClass*, ...)
    __attribute__ ((noinline));
  sint64 invokeLongStatic(Jnjvm* vm, UserClass*, ...)
    __attribute__ ((noinline));
  JavaObject* invokeJavaObjectStatic(Jnjvm* vm, UserClass*, ...)
    __attribute__ ((noinline));
  
  mvm::JITInfo* JInfo;
  template<typename Ty> 
  Ty *getInfo() {
    if (!JInfo) {
      JInfo = new(classDef->classLoader->allocator, "Method JIT info") Ty(this);
    }   

    assert((void*)dynamic_cast<Ty*>(JInfo) == (void*)JInfo &&
           "Invalid concrete type or multiple inheritence for getInfo");
    return static_cast<Ty*>(JInfo);
  }
  
  void clearInfo() {
    if (JInfo) JInfo->clear();
  }
  
  #define JNI_NAME_PRE "Java_"
  #define JNI_NAME_PRE_LEN 5
  
};

/// JavaField - This class represents a Java field.
///
class JavaField  : public mvm::PermanentObject {
private:
  /// _signature - The signature of the field. Null if not resolved.
  ///
  Typedef* _signature;
  
  /// InitField - Set an initial value to the field of an object.
  ///
  void InitField(void* obj, uint64 val = 0);
  void InitField(void* obj, JavaObject* val);
  void InitField(void* obj, double val);
  void InitField(void* obj, float val);

public:
  
  /// constructField - Create a new field.
  ///
  void initialise(Class* cl, const UTF8* name, const UTF8* type, uint16 access);

  /// ~JavaField - Destroy the field as well as its attributs.
  ///
  ~JavaField();

  /// access - The Java access type of this field (e.g. public, private).
  ///
  uint16 access;

  /// name - The name of the field.
  ///
  const UTF8* name;

  /// type - The UTF8 type name of the field.
  ///
  const UTF8* type;

  /// attributs - List of Java attributs for this field.
  ///
  Attribut* attributs;
  
  /// nbAttributs - The number of attributes.
  ///
  uint16 nbAttributs;

  /// classDef - The class where the field is defined.
  ///
  Class* classDef;

  /// ptrOffset - The offset of the field when the object containing
  /// the field is casted to an array of bytes.
  ///
  uint32 ptrOffset;
  
  /// num - The index of the field in the field list.
  ///
  uint16 num;
  
  /// getSignature - Get the signature of this field, resolving it if
  /// necessary.
  ///
  Typedef* getSignature() {
    if(!_signature)
      _signature = classDef->classLoader->constructType(type);
    return _signature;
  }

  /// initField - Init the value of the field in the given object. This is
  /// used for static fields which have a default value.
  ///
  void initField(void* obj, Jnjvm* vm);

  /// lookupAttribut - Look up the attribut in the field's list of attributs.
  ///
  Attribut* lookupAttribut(const UTF8* key);

  /// getVritual*Field - Get a virtual field of an object.
  ///
  #define GETFIELD(TYPE, TYPE_NAME) \
  TYPE get##TYPE_NAME##Field(void* obj) { \
    assert(classDef->isResolved()); \
    void* ptr = (void*)((uint64)obj + ptrOffset); \
    return ((TYPE*)ptr)[0]; \
  }

  /// set*Field - Set a field of an object.
  ///
  #define SETFIELD(TYPE, TYPE_NAME) \
  void set##TYPE_NAME##Field(void* obj, TYPE val) { \
    assert(classDef->isResolved()); \
    void* ptr = (void*)((uint64)obj + ptrOffset); \
    ((TYPE*)ptr)[0] = val; \
  }

  #define MK_ASSESSORS(TYPE, TYPE_NAME) \
    GETFIELD(TYPE, TYPE_NAME) \
    SETFIELD(TYPE, TYPE_NAME) \

  MK_ASSESSORS(float, Float);
  MK_ASSESSORS(double, Double);
  MK_ASSESSORS(JavaObject*, Object);
  MK_ASSESSORS(uint8, Int8);
  MK_ASSESSORS(uint16, Int16);
  MK_ASSESSORS(uint32, Int32);
  MK_ASSESSORS(sint64, Long);
  
  mvm::JITInfo* JInfo;
  template<typename Ty> 
  Ty *getInfo() {
    if (!JInfo) {
      JInfo = new(classDef->classLoader->allocator, "Field JIT info") Ty(this);
    }   

    assert((void*)dynamic_cast<Ty*>(JInfo) == (void*)JInfo &&
           "Invalid concrete type or multiple inheritence for getInfo");
    return static_cast<Ty*>(JInfo);
  }
  
  void clearInfo() {
    if (JInfo) JInfo->clear();
  }


  bool isReference() {
    uint16 val = type->elements[0];
    return (val == '[' || val == 'L');
  }

};


} // end namespace jnjvm


#ifdef ISOLATE_SHARING
#include "IsolateCommonClass.h"
#endif

#endif
