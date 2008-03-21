//===----------- GC.h - Garbage Collection Interface -----------------------===//
//
//                     The Micro Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#ifndef MVM_GC_H
#define MVM_GC_H

#include <sys/types.h>

typedef void (*gc_lock_recovery_fct_t)(int, int, int, int, int, int, int, int);

class gc;

typedef void VirtualTable;
#define  gc_new(Class)  __gc_new(Class::VT) Class

class gc_header {
public:
  VirtualTable *_XXX_vt;
	inline gc *_2gc() { return (gc *)this; }
};

class gc {
public:

	typedef void (*markerFn)(void);
	inline gc() {}
	virtual ~gc() {}

	static void	initialise(markerFn mark, void *base_sp);
	static void	destroy();
  void setVT(VirtualTable* VT) {
    ((void**)this)[0] = VT;
  }

	void	markAndTrace() const;
	size_t	objectSize() const;
	void *  operator new(size_t sz, VirtualTable *VT);
	void    operator delete(void *);
	void *  realloc(size_t n);

	static void           die_if_sigsegv_occured_during_collection(void *addr);
	static int            isStable(gc_lock_recovery_fct_t, int, int, int, int,
                                 int, int, int, int);
	static unsigned int   enable(unsigned int n);
	static void	          gcStats(size_t &no, size_t &nbb);
	static void	          maybeCollect();
	static void	          collect(void);
	static void           inject_my_thread(void *sp);
	static void           remove_my_thread();

	static gc             *begOf(const void *o);
	static int            byteOffset(void *o);
	inline static bool    isObject(const void *o) { return begOf((void*)o); }
        static void     applyFunc(void (*func)(gc *o, void *data), void *data);
        static void     registerMemoryError(void (*func)(unsigned int));
        static int      getMaxMemory(void);
        static int      getFreeMemory(void);
        static int      getTotalMemory(void);
        static void     setMaxMemory(size_t);
        static void     setMinMemory(size_t);
	
  virtual void					destroyer(size_t sz) {} ;
	virtual void					tracer(size_t sz) {};
};

#define __gc_new new

#endif
