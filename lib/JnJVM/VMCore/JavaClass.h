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

#include <vector>

#include "types.h"

#include "mvm/Method.h"
#include "mvm/Object.h"
#include "mvm/PrintBuffer.h"
#include "mvm/Threads/Cond.h"
#include "mvm/Threads/Locks.h"

#include "JavaAccess.h"

namespace jnjvm {

class ArrayUInt8;
class AssessorDesc;
class Enveloppe;
class Class;
class JavaCtpInfo;
class JavaField;
class JavaJIT;
class JavaMethod;
class JavaObject;
class Jnjvm;
class Reader;
class Signdef;
class Typedef;
class UTF8;


typedef enum JavaState {
  hashed = 0, loaded, readed, prepared, resolved, clinitParent, inClinit, ready
}JavaState;


class Attribut : public mvm::Object {
public:
  static VirtualTable* VT;
  const UTF8* name;
  unsigned int start;
  unsigned int  nbb;

  void derive(const UTF8* name, unsigned int length, const Reader* reader);
  static Attribut* lookup(const std::vector<Attribut*, gc_allocator<Attribut*> > * vec,
                          const UTF8* key);
  Reader* toReader(Jnjvm *vm, ArrayUInt8* array, Attribut* attr);

  static const UTF8* codeAttribut;
  static const UTF8* exceptionsAttribut;
  static const UTF8* constantAttribut;
  static const UTF8* lineNumberTableAttribut;
  static const UTF8* innerClassesAttribut;
  static const UTF8* sourceFileAttribut;
  
  virtual void print(mvm::PrintBuffer *buf) const;
  virtual void TRACER;
};

class CommonClass : public mvm::Object {
public:
  
  /// virtualSize - The size of instances of this class.
  ///
  uint32 virtualSize;

  /// virtualVT - The virtual table of instances of this class.
  ///
  VirtualTable* virtualVT;
  
  /// virtualTableSize - The size of the virtual table of this class.
  ///
  uint32 virtualTableSize;
  
  /// access - {public, private, protected}.
  ///
  uint32 access;
  
  /// isArray - Is the class an array class?
  ///
  uint8 isArray;
  
  /// name - The name of the class.
  ///
  const UTF8* name;
  
  /// isolate - Which isolate defined this class.
  ///
  Jnjvm *isolate;
  
  /// status - The loading/resolve/initialization state of the class.
  ///
  JavaState status;
  
  /// super - The parent of this class.
  ///
  CommonClass * super;

  /// superUTF8 - The name of the parent of this class.
  ///
  const UTF8* superUTF8;
  
  /// interfaces - The interfaces this class implements.
  ///
  std::vector<Class*> interfaces;

  /// interfacesUTF8 - The names of the interfaces this class implements.
  ///
  std::vector<const UTF8*> interfacesUTF8;
  
  /// lockVar - When multiple threads want to load/resolve/initialize a class,
  /// they must be synchronized so that these steps are only performned once
  /// for a given class.
  mvm::Lock* lockVar;

  /// condVar - Used to wake threads waiting on the load/resolve/initialize
  /// process of this class, done by another thread.
  mvm::Cond* condVar;
  
  /// classLoader - The Java class loader that loaded the class.
  ///
  JavaObject* classLoader;
  
#ifndef MULTIPLE_VM
  /// delegatee - The java/lang/Class object representing this class
  ///
  JavaObject* delegatee;
#endif
  
  /// virtualMethods - List of all the virtual methods defined by this class.
  /// This does not contain non-redefined super methods.
  std::vector<JavaMethod*> virtualMethods;

  /// staticMethods - List of all the static methods defined by this class.
  ///
  std::vector<JavaMethod*> staticMethods;

  /// virtualFields - List of all the virtual fields defined in this class.
  /// This does not contain non-redefined super fields.
  std::vector<JavaField*>  virtualFields;

  /// staticFields - List of all the static fields defined in this class.
  ///
  std::vector<JavaField*>  staticFields;
  
  /// display - The class hierarchy of supers for this class.
  ///
  std::vector<CommonClass*> display;

  /// depth - The depth of this class in its class hierarchy. 
  /// display[depth - 1] contains the class.
  uint32 depth;
  
  static void printClassName(const UTF8* name, mvm::PrintBuffer* buf);
  void initialise(Jnjvm* isolate, bool array);
  void aquire(); 
  void release();
  void waitClass();
  void broadcastClass();
  bool ownerClass();
  
  JavaMethod* lookupMethodDontThrow(const UTF8* name, const UTF8* type,
                                    bool isStatic, bool recurse);
  
  JavaMethod* lookupMethod(const UTF8* name, const UTF8* type, bool isStatic,
                           bool recurse);
  
  JavaField* lookupFieldDontThrow(const UTF8* name, const UTF8* type,
                                  bool isStatic, bool recurse);
  
  JavaField* lookupField(const UTF8* name, const UTF8* type, bool isStatic,
                         bool recurse);


  virtual void print(mvm::PrintBuffer *buf) const;
  virtual void TRACER;

  bool inheritName(const UTF8* Tname);
  bool isOfTypeName(const UTF8* Tname);
  bool implements(CommonClass* cl);
  bool instantiationOfArray(CommonClass* cl);
  bool subclassOf(CommonClass* cl);
  bool isAssignableFrom(CommonClass* cl);
  JavaObject* getClassDelegatee();
  void initialiseClass();
  void resolveClass(bool doClinit);

#ifndef MULTIPLE_VM
  JavaState* getStatus() {
    return &status;
  }
  bool isReady() {
    return status == ready;
  }
#else
  JavaState* getStatus();
  bool isReady() {
    return *getStatus() == ready;
  }
#endif
  bool isResolved() {
    return status >= resolved;
  }
  
  static VirtualTable* VT;
  static const int MaxDisplay;
  static JavaObject* jnjvmClassLoader;
};

class Class : public CommonClass {
public:
  static VirtualTable* VT;
  unsigned int minor;
  unsigned int major;
  ArrayUInt8* bytes;
  mvm::Code* codeVirtualTracer;
  mvm::Code* codeStaticTracer;
  JavaCtpInfo* ctpInfo;
  std::vector<Attribut*, gc_allocator<Attribut*> > attributs;
  std::vector<Class*> innerClasses;
  Class* outerClass;
  uint32 innerAccess;
  bool innerOuterResolved;
  
  uint32 staticSize;
  VirtualTable* staticVT;
  JavaObject* doNew(Jnjvm* vm);
  virtual void print(mvm::PrintBuffer *buf) const;
  virtual void TRACER;

  JavaObject* operator()(Jnjvm* vm);

#ifndef MULTIPLE_VM
  JavaObject* _staticInstance;
  JavaObject* staticInstance() {
    return _staticInstance;
  }
  void createStaticInstance() { }
#else
  JavaObject* staticInstance();
  void createStaticInstance();
#endif

};


class ClassArray : public CommonClass {
public:
  static VirtualTable* VT;
  CommonClass*  _baseClass;
  AssessorDesc* _funcs;

  void resolveComponent();
  CommonClass* baseClass();
  AssessorDesc* funcs();
  static JavaObject* arrayLoader(Jnjvm* isolate, const UTF8* name,
                                 JavaObject* loader, unsigned int start,
                                 unsigned int end);

  virtual void print(mvm::PrintBuffer *buf) const;
  virtual void TRACER;

  static CommonClass* SuperArray;
  static std::vector<Class*>        InterfacesArray;
  static std::vector<JavaMethod*>   VirtualMethodsArray;
  static std::vector<JavaMethod*>   StaticMethodsArray;
  static std::vector<JavaField*>    VirtualFieldsArray;
  static std::vector<JavaField*>    StaticFieldsArray;
};


class JavaMethod : public mvm::Object {
public:
  static VirtualTable* VT;
  unsigned int access;
  Signdef* signature;
  std::vector<Attribut*, gc_allocator<Attribut*> > attributs;
  std::vector<Enveloppe*, gc_allocator<Enveloppe*> > caches;
  Class* classDef;
  const UTF8* name;
  const UTF8* type;
  bool canBeInlined;
  mvm::Code* code;
  
  /// offset - The index of the method in the virtual table.
  ///
  uint32 offset;

  virtual void print(mvm::PrintBuffer *buf) const;
  virtual void TRACER;

  void* compiledPtr();

  uint32 invokeIntSpecialAP(Jnjvm* vm, JavaObject* obj, va_list ap);
  float invokeFloatSpecialAP(Jnjvm* vm, JavaObject* obj, va_list ap);
  double invokeDoubleSpecialAP(Jnjvm* vm, JavaObject* obj, va_list ap);
  sint64 invokeLongSpecialAP(Jnjvm* vm, JavaObject* obj, va_list ap);
  JavaObject* invokeJavaObjectSpecialAP(Jnjvm* vm, JavaObject* obj, va_list ap);
  
  uint32 invokeIntVirtualAP(Jnjvm* vm, JavaObject* obj, va_list ap);
  float invokeFloatVirtualAP(Jnjvm* vm, JavaObject* obj, va_list ap);
  double invokeDoubleVirtualAP(Jnjvm* vm, JavaObject* obj, va_list ap);
  sint64 invokeLongVirtualAP(Jnjvm* vm, JavaObject* obj, va_list ap);
  JavaObject* invokeJavaObjectVirtualAP(Jnjvm* vm, JavaObject* obj, va_list ap);
  
  uint32 invokeIntStaticAP(Jnjvm* vm, va_list ap);
  float invokeFloatStaticAP(Jnjvm* vm, va_list ap);
  double invokeDoubleStaticAP(Jnjvm* vm, va_list ap);
  sint64 invokeLongStaticAP(Jnjvm* vm, va_list ap);
  JavaObject* invokeJavaObjectStaticAP(Jnjvm* vm, va_list ap);
  
  uint32 invokeIntSpecialBuf(Jnjvm* vm, JavaObject* obj, void* buf);
  float invokeFloatSpecialBuf(Jnjvm* vm, JavaObject* obj, void* buf);
  double invokeDoubleSpecialBuf(Jnjvm* vm, JavaObject* obj, void* buf);
  sint64 invokeLongSpecialBuf(Jnjvm* vm, JavaObject* obj, void* buf);
  JavaObject* invokeJavaObjectSpecialBuf(Jnjvm* vm, JavaObject* obj, void* buf);
  
  uint32 invokeIntVirtualBuf(Jnjvm* vm, JavaObject* obj, void* buf);
  float invokeFloatVirtualBuf(Jnjvm* vm, JavaObject* obj, void* buf);
  double invokeDoubleVirtualBuf(Jnjvm* vm, JavaObject* obj, void* buf);
  sint64 invokeLongVirtualBuf(Jnjvm* vm, JavaObject* obj, void* buf);
  JavaObject* invokeJavaObjectVirtualBuf(Jnjvm* vm, JavaObject* obj, void* buf);
  
  uint32 invokeIntStaticBuf(Jnjvm* vm, void* buf);
  float invokeFloatStaticBuf(Jnjvm* vm, void* buf);
  double invokeDoubleStaticBuf(Jnjvm* vm, void* buf);
  sint64 invokeLongStaticBuf(Jnjvm* vm, void* buf);
  JavaObject* invokeJavaObjectStaticBuf(Jnjvm* vm, void* buf);
  
  uint32 invokeIntSpecial(Jnjvm* vm, JavaObject* obj, ...);
  float invokeFloatSpecial(Jnjvm* vm, JavaObject* obj, ...);
  double invokeDoubleSpecial(Jnjvm* vm, JavaObject* obj, ...);
  sint64 invokeLongSpecial(Jnjvm* vm, JavaObject* obj, ...);
  JavaObject* invokeJavaObjectSpecial(Jnjvm* vm, JavaObject* obj, ...);
  
  uint32 invokeIntVirtual(Jnjvm* vm, JavaObject* obj, ...);
  float invokeFloatVirtual(Jnjvm* vm, JavaObject* obj, ...);
  double invokeDoubleVirtual(Jnjvm* vm, JavaObject* obj, ...);
  sint64 invokeLongVirtual(Jnjvm* vm, JavaObject* obj, ...);
  JavaObject* invokeJavaObjectVirtual(Jnjvm* vm, JavaObject* obj, ...);
  
  uint32 invokeIntStatic(Jnjvm* vm, ...);
  float invokeFloatStatic(Jnjvm* vm, ...);
  double invokeDoubleStatic(Jnjvm* vm, ...);
  sint64 invokeLongStatic(Jnjvm* vm, ...);
  JavaObject* invokeJavaObjectStatic(Jnjvm* vm, ...);
};

class JavaField : public mvm::Object {
public:
  static VirtualTable* VT;
  unsigned int access;
  const UTF8* name;
  Typedef* signature;
  const UTF8* type;
  std::vector<Attribut*, gc_allocator<Attribut*> > attributs;
  Class* classDef;
  uint64 ptrOffset;
  /// num - The index of the field in the field list.
  ///
  uint32 num;

  void initField(JavaObject* obj);

  virtual void print(mvm::PrintBuffer *buf) const;
  virtual void TRACER;
  
  void setStaticFloatField(float val);
  void setStaticDoubleField(double val);
  void setStaticInt8Field(uint8 val);
  void setStaticInt16Field(uint16 val);
  void setStaticInt32Field(uint32 val);
  void setStaticLongField(sint64 val);
  void setStaticObjectField(JavaObject* val);

  float getStaticFloatField();
  double getStaticDoubleField();
  uint8 getStaticInt8Field();
  uint16 getStaticInt16Field();
  uint32 getStaticInt32Field();
  sint64 getStaticLongField();
  JavaObject* getStaticObjectField();
  
  void setVirtualFloatField(JavaObject* obj, float val);
  void setVirtualDoubleField(JavaObject* obj, double val);
  void setVirtualInt8Field(JavaObject* obj, uint8 val);
  void setVirtualInt16Field(JavaObject* obj, uint16 val);
  void setVirtualInt32Field(JavaObject* obj, uint32 val);
  void setVirtualLongField(JavaObject* obj, sint64 val);
  void setVirtualObjectField(JavaObject* obj, JavaObject* val);
  
  float getVirtualFloatField(JavaObject* obj);
  double getVirtualDoubleField(JavaObject* obj);
  uint8  getVirtualInt8Field(JavaObject* obj);
  uint16 getVirtualInt16Field(JavaObject* obj);
  uint32 getVirtualInt32Field(JavaObject* obj);
  sint64 getVirtualLongField(JavaObject* obj);
  JavaObject* getVirtualObjectField(JavaObject* obj);
  
};


} // end namespace jnjvm

#endif
