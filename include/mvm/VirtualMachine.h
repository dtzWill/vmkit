//===--------- VirtualMachine.h - Registering a VM ------------------------===//
//
//                            The VMKit project
//
// This file is distributed under the University of Pierre et Marie Curie 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Ultimately, this would be like a generic way of defining a VM. But we're not
// quite there yet.
//
//===----------------------------------------------------------------------===//

#ifndef MVM_VIRTUALMACHINE_H
#define MVM_VIRTUALMACHINE_H

#include "mvm/Object.h"

namespace mvm {

/// VirtualMachine - This class is the root of virtual machine classes. It
/// defines what a VM should be. Currently, a VM only initializes itself
/// and runs applications.
///
class VirtualMachine : public mvm::Object {
public:
  
  /// runApplication - Run an application. The application name is in
  /// the arguments, hence it is the virtual machine's job to parse them.
  virtual void runApplication(int argc, char** argv) = 0;

  static void initialiseJVM();
  static VirtualMachine* createJVM();
  
  static void initialiseCLIVM();
  static VirtualMachine* createCLIVM();
  
};


} // end namespace mvm
#endif // MVM_VIRTUALMACHINE_H
