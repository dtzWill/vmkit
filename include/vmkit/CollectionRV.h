//===---------- CollectionRV.h - Rendez-vous for garbage collection -------===//
//
//                            The VMKit project
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef _VMKIT_COLLECTIONRV_H_
#define _VMKIT_COLLECTIONRV_H_

#include "vmkit/Cond.h"
#include "vmkit/Locks.h"
#include "vmkit/Thread.h"

namespace vmkit {

class CollectionRV {
protected: 
  /// _lockRV - Lock for synchronization.
  LockNormal _lockRV;         
  
  /// condEndRV - Condition for unlocking other tasks (write protect).
  Cond condEndRV;

  /// collectionCond - Condition for unblocking the initator.
  Cond condInitiator;

  /// nbJoined - Number of threads that joined the rendezvous.
  unsigned nbJoined;

  // initiator - The initiator of the rendesvous.
  Thread* initiator;
  
public: 
  CollectionRV() {
    nbJoined = 0;
    initiator = NULL;
  }

  void lockRV() { _lockRV.lock(); }
  void unlockRV() { _lockRV.unlock(); }

  void waitEndOfRV();
  void waitRV();
  
  void startRV() {
    vmkit::Thread::get()->inRV = true;
    lockRV();
  }

  void cancelRV() {
    unlockRV();
    vmkit::Thread::get()->inRV = false;
  }
  
  void another_mark();
  Thread* getInitiator() const { return initiator; }

  virtual void finishRV() = 0;
  virtual void synchronize() = 0;

  virtual void join() = 0;
  virtual void joinAfterUncooperative(word_t SP) = 0;
  virtual void joinBeforeUncooperative() = 0;

  virtual void addThread(Thread* th) = 0;
};

class CooperativeCollectionRV : public CollectionRV {
public:
  void finishRV();
  void synchronize();

  void join();
  void joinAfterUncooperative(word_t SP);
  void joinBeforeUncooperative();
  void addThread(Thread* th);

};

}

#endif
