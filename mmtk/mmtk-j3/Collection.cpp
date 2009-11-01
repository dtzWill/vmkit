//===------- Collection.cpp - Implementation of the Collection class  -----===//
//
//                              The VMKit project
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "JavaObject.h"
#include "JavaThread.h"

using namespace jnjvm;

extern "C" void JnJVM_org_mmtk_plan_Plan_setCollectionTriggered__();

extern "C" void JnJVM_org_j3_config_Selected_00024Collector_staticCollect__();

extern "C" void JnJVM_org_mmtk_plan_Plan_collectionComplete__();

extern "C" uint8_t JnJVM_org_mmtk_utility_heap_HeapGrowthManager_considerHeapSize__();


extern "C" bool Java_org_j3_mmtk_Collection_isEmergencyAllocation__ (JavaObject* C) {
  // TODO: emergency when OOM.
  return false;
}

extern "C" void Java_org_j3_mmtk_Collection_reportAllocationSuccess__ (JavaObject* C) {
  // TODO: clear internal data.
}


extern "C" void Java_org_j3_mmtk_Collection_triggerCollection__I (JavaObject* C, int why) {
  mvm::Thread* th = mvm::Thread::get();
 
  th->MyVM->startCollection();
  th->MyVM->rendezvous.synchronize();
  
  JnJVM_org_mmtk_plan_Plan_setCollectionTriggered__();
  JnJVM_org_j3_config_Selected_00024Collector_staticCollect__();
  JnJVM_org_mmtk_utility_heap_HeapGrowthManager_considerHeapSize__();
  JnJVM_org_mmtk_plan_Plan_collectionComplete__();

  th->MyVM->rendezvous.finishRV();
  
  th->MyVM->endCollection();
  
  th->MyVM->wakeUpFinalizers();
  th->MyVM->wakeUpEnqueue();

}

extern "C" void Java_org_j3_mmtk_Collection_joinCollection__ (JavaObject* C) {
  mvm::Thread* th = mvm::Thread::get();
  assert(th->inRV && "Joining collection without a rendezvous");
  th->MyVM->rendezvous.join();
}

extern "C" int Java_org_j3_mmtk_Collection_rendezvous__I (JavaObject* C, int where) {
  return 1;
}

extern "C" int Java_org_j3_mmtk_Collection_maximumCollectionAttempt__ (JavaObject* C) {
  return 0;
}

extern "C" void Java_org_j3_mmtk_Collection_prepareCollector__Lorg_mmtk_plan_CollectorContext_2 (JavaObject* C, JavaObject* CC) {
  // Nothing to do.
}

extern "C" void Java_org_j3_mmtk_Collection_prepareMutator__Lorg_mmtk_plan_MutatorContext_2 (JavaObject* C, JavaObject* MC) {
}


extern "C" void Java_org_j3_mmtk_Collection_reportPhysicalAllocationFailed__ () { JavaThread::get()->printBacktrace(); abort(); }
extern "C" void Java_org_j3_mmtk_Collection_triggerAsyncCollection__I () { JavaThread::get()->printBacktrace(); abort(); }
extern "C" void Java_org_j3_mmtk_Collection_noThreadsInGC__ () { JavaThread::get()->printBacktrace(); abort(); }
extern "C" void Java_org_j3_mmtk_Collection_activeGCThreads__ () { JavaThread::get()->printBacktrace(); abort(); }
extern "C" void Java_org_j3_mmtk_Collection_activeGCThreadOrdinal__ () { JavaThread::get()->printBacktrace(); abort(); }
extern "C" void Java_org_j3_mmtk_Collection_requestMutatorFlush__ () { JavaThread::get()->printBacktrace(); abort(); }
