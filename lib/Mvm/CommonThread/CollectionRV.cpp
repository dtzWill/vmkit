//===-------- CollectionRV.cpp - Rendez-vous for garbage collection -------===//
//
//                            The VMKit project
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include "MvmGC.h"
#include "mvm/VirtualMachine.h"
#include "mvm/Threads/CollectionRV.h"

#include "debug.h"

using namespace mvm;

void CollectionRV::another_mark() {
  mvm::Thread* th = mvm::Thread::get();
  assert(th->getLastSP() != NULL);
  assert(nbJoined < th->MyVM->NumberOfThreads);
  nbJoined++;
  if (nbJoined == th->MyVM->NumberOfThreads) {
    condInitiator.broadcast();
  }
}

void CollectionRV::waitEndOfRV() {
  mvm::Thread* th = mvm::Thread::get();
  assert(th->getLastSP() != NULL);

  while (th->doYield) {
    condEndRV.wait(&_lockRV);
  }
}

void CollectionRV::waitRV() {
  mvm::Thread* self = mvm::Thread::get(); 
  // Add myself.
  nbJoined++;

  while (nbJoined != self->MyVM->NumberOfThreads) {
    condInitiator.wait(&_lockRV);
  } 
}

void CooperativeCollectionRV::synchronize() {
  assert(nbJoined == 0);
  mvm::Thread* self = mvm::Thread::get();
  // Lock thread lock, so that we can traverse the thread list safely. This will
  // be released on finishRV.
  self->MyVM->ThreadLock.lock();

  mvm::Thread* cur = self;
  do {
    assert(!cur->doYield);
    cur->doYield = true;
    assert(!cur->joinedRV);
    cur = (mvm::Thread*)cur->next();
  } while (cur != self);

  self->joinedRV = true; 
  // Lookup currently blocked threads.
  for (cur = (mvm::Thread*)self->next(); cur != self; 
       cur = (mvm::Thread*)cur->next()) {
    if (cur->getLastSP()) {
      nbJoined++;
      cur->joinedRV = true;
    }
  }
  
  // And wait for other threads to finish.
  waitRV();

  // Unlock, so that threads in uncooperative code that go back to cooperative
  // code can set back their lastSP.
  unlockRV();
}

void UncooperativeCollectionRV::synchronize() { 
  assert(nbJoined == 0);
  mvm::Thread* self = mvm::Thread::get();
  // Lock thread lock, so that we can traverse the thread list safely. This will
  // be released on finishRV.
  self->MyVM->ThreadLock.lock();
  
  for (mvm::Thread* cur = (mvm::Thread*)self->next(); cur != self; 
       cur = (mvm::Thread*)cur->next()) {
    cur->killForRendezvous();
  }
  
  // And wait for other threads to finish.
  waitRV();

  // Unlock, so that threads in uncooperative code that go back to cooperative
  // code can set back their lastSP.
  unlockRV();
}


void UncooperativeCollectionRV::join() {
  mvm::Thread* th = mvm::Thread::get();
  th->inRV = true;

  lockRV();
  void* old = th->getLastSP();
  th->setLastSP(FRAME_PTR());
  another_mark();
  waitEndOfRV();
  th->setLastSP(old);
  unlockRV();

  th->inRV = false;
}

void CooperativeCollectionRV::join() {
  mvm::Thread* th = mvm::Thread::get();
  assert(th->doYield && "No yield");
  assert((th->getLastSP() == NULL) && "SP present in cooperative code");

  th->inRV = true;
  
  lockRV();
  th->setLastSP(FRAME_PTR());
  th->joinedRV = true;
  another_mark();
  waitEndOfRV();
  th->setLastSP(0);
  unlockRV();
  
  th->inRV = false;
}

void CooperativeCollectionRV::joinBeforeUncooperative() {
  mvm::Thread* th = mvm::Thread::get();
  assert((th->getLastSP() != NULL) &&
         "SP not set before entering uncooperative code");

  th->inRV = true;
  
  lockRV();
  if (th->doYield) {
    if (!th->joinedRV) {
      th->joinedRV = true;
      another_mark();
    }
    waitEndOfRV();
  }
  unlockRV();

  th->inRV = false;
}

void CooperativeCollectionRV::joinAfterUncooperative() {
  mvm::Thread* th = mvm::Thread::get();
  assert((th->getLastSP() == NULL) &&
         "SP set after entering uncooperative code");

  th->inRV = true;

  lockRV();
  if (th->doYield) {
    th->setLastSP(FRAME_PTR());
    if (!th->joinedRV) {
      th->joinedRV = true;
      another_mark();
    }
    waitEndOfRV();
    th->setLastSP(NULL);
  }
  unlockRV();

  th->inRV = false;
}

extern "C" void conditionalSafePoint() {
  mvm::Thread* th = mvm::Thread::get();
  th->MyVM->rendezvous.join();
}

void CooperativeCollectionRV::finishRV() {
  lockRV();
    
  mvm::Thread* initiator = mvm::Thread::get();
  mvm::Thread* cur = initiator;
  do {
    assert(cur->doYield && "Inconsistent state");
    assert(cur->joinedRV && "Inconsistent state");
    cur->doYield = false;
    cur->joinedRV = false;
    cur = (mvm::Thread*)cur->next();
  } while (cur != initiator);

  assert(nbJoined == initiator->MyVM->NumberOfThreads && "Inconsistent state");
  nbJoined = 0;
  initiator->MyVM->ThreadLock.unlock();
  condEndRV.broadcast();
  unlockRV();
  initiator->inRV = false;
}

void UncooperativeCollectionRV::finishRV() {
  lockRV();
  mvm::Thread* initiator = mvm::Thread::get();
  assert(nbJoined == initiator->MyVM->NumberOfThreads && "Inconsistent state");
  nbJoined = 0;
  initiator->MyVM->ThreadLock.unlock();
  condEndRV.broadcast();
  unlockRV();
  initiator->inRV = false;
}

void UncooperativeCollectionRV::joinAfterUncooperative() {
  UNREACHABLE();
}

void UncooperativeCollectionRV::joinBeforeUncooperative() {
  UNREACHABLE();
}
