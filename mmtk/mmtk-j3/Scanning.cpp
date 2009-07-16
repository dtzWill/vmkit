//===-------- Scanning.cpp - Implementation of the Scanning class  --------===//
//
//                              The VMKit project
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "JavaObject.h"

using namespace jnjvm;

extern "C" void Java_org_j3_mmtk_Scanning_computeThreadRoots__Lorg_mmtk_plan_TraceLocal_2 () {
}

extern "C" void Java_org_j3_mmtk_Scanning_computeGlobalRoots__Lorg_mmtk_plan_TraceLocal_2 () {
}

extern "C" void Java_org_j3_mmtk_Scanning_computeStaticRoots__Lorg_mmtk_plan_TraceLocal_2 () {
}

extern "C" void Java_org_j3_mmtk_Scanning_resetThreadCounter__ () {
}

extern "C" void Java_org_j3_mmtk_Scanning_scanObject__Lorg_mmtk_plan_TransitiveClosure_2Lorg_vmmagic_unboxed_ObjectReference_2 () { abort(); }
extern "C" void Java_org_j3_mmtk_Scanning_specializedScanObject__ILorg_mmtk_plan_TransitiveClosure_2Lorg_vmmagic_unboxed_ObjectReference_2 () { abort(); }
extern "C" void Java_org_j3_mmtk_Scanning_precopyChildren__Lorg_mmtk_plan_TraceLocal_2Lorg_vmmagic_unboxed_ObjectReference_2 () { abort(); }
extern "C" void Java_org_j3_mmtk_Scanning_preCopyGCInstances__Lorg_mmtk_plan_TraceLocal_2 () { abort(); }
