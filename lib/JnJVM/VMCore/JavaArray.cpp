//===----------------- JavaArray.cpp - Java arrays ------------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <cstdarg>
#include <cstdlib>

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaObject.h"
#include "JavaTypes.h"
#include "Jnjvm.h"
#include "JavaThread.h"
#include "JavaUpcalls.h"
#include "LockedMap.h"


using namespace jnjvm;

/// This value is the same value than IBM's JVM.
const sint32 JavaArray::MaxArraySize = 268435455;

/// The JVM defines constants for referencing arrays of primitive types.
const unsigned int JavaArray::T_BOOLEAN = 4;
const unsigned int JavaArray::T_CHAR = 5;
const unsigned int JavaArray::T_FLOAT = 6;
const unsigned int JavaArray::T_DOUBLE = 7;
const unsigned int JavaArray::T_BYTE = 8;
const unsigned int JavaArray::T_SHORT = 9;
const unsigned int JavaArray::T_INT = 10;
const unsigned int JavaArray::T_LONG = 11;
