//===------- LockedMap.h - A thread-safe map implementation ---------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines thread-safe maps that must be deallocated by the owning
// object. For example a class loader is responsible for deallocating the
// types stored in a TypeMap.
//
//===----------------------------------------------------------------------===//

#ifndef JNJVM_LOCKED_MAP_H
#define JNJVM_LOCKED_MAP_H

#include <map>

#include <cstring>

#include "types.h"

#include "mvm/Allocator.h"
#include "mvm/Threads/Locks.h"

#include "JavaArray.h" // for comparing UTF8s

namespace jnjvm {

class JavaString;
class Signdef;
class Typedef;
class UserCommonClass;
class UserClassArray;

struct ltutf8
{
  bool operator()(const UTF8* s1, const UTF8* s2) const
  {
    return s1->lessThan(s2);
  }
};

struct ltarray16
{
  bool operator()(const ArrayUInt16* s1, const ArrayUInt16* s2) const
  {
    if (s1->size < s2->size) return true;
    else if (s1->size > s2->size) return false;
    else return memcmp((const char*)s1->elements, (const char*)s2->elements,
                       s1->size * sizeof(uint16)) < 0;
  }
};

template<class Key, class Container, class Compare, class Meta, class TLock>
class LockedMap : public mvm::PermanentObject {
public:
  typedef typename std::map<const Key, Container, Compare>::iterator iterator;
  typedef Container (*funcCreate)(Key& V, Meta meta);

  TLock lock;
  std::map<const Key, Container, Compare,
           gc_allocator<std::pair<const Key, Container> > > map;
  
  inline Container lookupOrCreate(Key& V, Meta meta, funcCreate func) {
    lock.lock();
    iterator End = map.end();
    iterator I = map.find(V);
    if (I == End) {
      Container res = func(V, meta);
      map.insert(std::make_pair(V, res));
      lock.unlock();
      return res;
    } else {
      lock.unlock();
      return ((Container)(I->second));
    }
  }
  
  inline void remove(Key V) {
    lock.lock();
    map.erase(V);
    lock.unlock();
  }
  
  inline void remove(Key V, Container C) {
    lock.lock();
    removeUnlocked(V, C); 
    lock.unlock();
  }
  
  inline void removeUnlocked(Key V, Container C) {
    iterator End = map.end();
    iterator I = map.find(V);
    
    if (I != End && I->second == C)
        map.erase(I); 
  }

  inline Container lookup(Key V) {
    lock.lock();
    iterator End = map.end();
    iterator I = map.find(V);
    lock.unlock();
    return I != End ? ((Container)(I->second)) : 0; 
  }

  inline void hash(Key k, Container c) {
    lock.lock();
    map.insert(std::make_pair(k, c));
    lock.unlock();
  }

  ~LockedMap() {}
};

class UTF8Map : public mvm::PermanentObject {
public:
  typedef std::multimap<const uint32, const UTF8*>::iterator iterator;
  
  mvm::LockNormal lock;
  mvm::BumpPtrAllocator& allocator;
  std::multimap<const uint32, const UTF8*> map;
  const UTF8* lookupOrCreateAsciiz(const char* asciiz); 
  const UTF8* lookupOrCreateReader(const uint16* buf, uint32 size);
  const UTF8* lookupAsciiz(const char* asciiz); 
  const UTF8* lookupReader(const uint16* buf, uint32 size);
  
  UTF8Map(mvm::BumpPtrAllocator& A) : allocator(A) {}

  ~UTF8Map() {
    for (iterator i = map.begin(), e = map.end(); i!= e; ++i) {
      allocator.Deallocate((void*)i->second);
    }
  }

  void copy(UTF8Map* newMap) {
    for (iterator i = map.begin(), e = map.end(); i!= e; ++i) {
      newMap->map.insert(*i);
    }
  }
  
  void replace(const UTF8* oldUTF8, const UTF8* newUTF8);
  void insert(const UTF8* val);
};

class ClassMap : 
  public LockedMap<const UTF8*, UserCommonClass*, ltutf8, JnjvmClassLoader*,
                   mvm::LockRecursive > {

#ifdef USE_GC_BOEHM
public:
  void* operator new(size_t sz, mvm::BumpPtrAllocator& allocator) {
    return GC_MALLOC(sz);
  }
#endif
};

class StringMap :
  public LockedMap<const ArrayUInt16*, JavaString*, ltarray16, Jnjvm*,
                   mvm::LockNormal> {

public:
  void insert(JavaString* str);

};

class TypeMap : public mvm::PermanentObject {
public:
  mvm::LockNormal lock;
  
  std::map<const UTF8*, Typedef*, ltutf8> map;
  typedef std::map<const UTF8*, Typedef*, ltutf8>::iterator iterator;
  
  inline Typedef* lookup(const UTF8* V) {
    iterator End = map.end();
    iterator I = map.find(V);
    return I != End ? I->second : 0; 
  }

  inline void hash(const UTF8* k, Typedef* c) {
    map.insert(std::make_pair(k, c));
  }
};

class SignMap : public mvm::PermanentObject {
public:
  mvm::LockNormal lock;
  
  std::map<const UTF8*, Signdef*, ltutf8> map;
  typedef std::map<const UTF8*, Signdef*, ltutf8>::iterator iterator;
  
  inline Signdef* lookup(const UTF8* V) {
    iterator End = map.end();
    iterator I = map.find(V);
    return I != End ? I->second : 0; 
  }

  inline void hash(const UTF8* k, Signdef* c) {
    map.insert(std::make_pair(k, c));
  }
  
};

} // end namespace jnjvm

#endif
