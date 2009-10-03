#ifndef _UTF8_INTERNAL_H_
#define _UTF8_INTERNAL_H_

#include <map>
#include "mvm/Allocator.h"

namespace mvm {

class UTF8Map;

class UTF8 {
  friend class UTF8Map;
private:
  
  /// operator new - Redefines the new operator of this class to allocate
  /// its objects in permanent memory, not with the garbage collector.
  void* operator new(size_t sz, mvm::BumpPtrAllocator& allocator, sint32 size) {
    return allocator.Allocate(sizeof(ssize_t) + size * sizeof(uint16), "UTF8");
  }
  
  UTF8(sint32 n) {
    size = n;
  }

public:
  /// size - The (constant) size of the array.
  ssize_t size;

  /// elements - Elements of this array. The size here is different than the
  /// actual size of the Java array. This is to facilitate Java array accesses
  /// in JnJVM code. The size should be set to zero, but this is invalid C99.
  uint16 elements[1];
  
  /// extract - Similar, but creates it in the map.
  const UTF8* extract(UTF8Map* map, uint32 start, uint32 len) const;
 
  /// equals - Are the two UTF8s equal?
  bool equals(const UTF8* other) const {
    if (other == this) return true;
    else if (size != other->size) return false;
    else return !memcmp(elements, other->elements, size * sizeof(uint16));
  }
  
  /// equals - Does the UTF8 equal to the buffer? 
  bool equals(const uint16* buf, sint32 len) const {
    if (size != len) return false;
    else return !memcmp(elements, buf, size * sizeof(uint16));
  }

  /// lessThan - strcmp-like function for UTF8s, used by hash tables.
  bool lessThan(const UTF8* other) const {
    if (size < other->size) return true;
    else if (size > other->size) return false;
    else return memcmp((const char*)elements, (const char*)other->elements, 
                       size * sizeof(uint16)) < 0;
  }
  
};


class UTF8Map : public mvm::PermanentObject {
private:
  typedef std::multimap<const uint32, const UTF8*>::iterator iterator;
  
  mvm::LockNormal lock;
  mvm::BumpPtrAllocator& allocator;
  std::multimap<const uint32, const UTF8*> map;
public:

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

/// UTF8Buffer - Helper class to create char* buffers suitable for
/// printf.
///
class UTF8Buffer {

  /// buffer - The buffer that holds a string representation.
  ///
  char* buffer;
public:

  /// UTF8Buffer - Create a buffer with the following UTF8.
  ///
  UTF8Buffer(const UTF8* val) {
    buffer = new char[val->size + 1];
    for (sint32 i = 0; i < val->size; ++i)
      buffer[i] = val->elements[i];
    buffer[val->size] = 0;
  }

  /// ~UTF8Buffer - Delete the buffer, as well as all dynamically
  /// allocated memory.
  ///
  ~UTF8Buffer() {
    delete[] buffer;
  }

  /// replaceWith - replace the content of the buffer and free the old buffer
  ///
	void replaceWith(char *buffer) {
		delete[] this->buffer;
		this->buffer = buffer;
	}


  /// cString - Return a C string representation of the buffer, suitable
  /// for printf.
  ///
  const char* cString() {
    return buffer;
  }
};

} // end namespace mvm

#endif
