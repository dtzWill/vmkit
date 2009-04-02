//===------------- llcj.cpp - Java ahead of time compiler -----------------===//
//
//                           The VMKit project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ManagedStatic.h"
#include "llvm/System/Path.h"
#include "llvm/System/Program.h"
#include "llvm/System/Signals.h"

#include "LinkPaths.h"

using namespace llvm;

int main(int argc, char **argv) {
  llvm_shutdown_obj X;  // Call llvm_shutdown() on exit.

  bool SaveTemps = false;
  char* opt = 0;
  
  const char** vmjcArgv = new const char*[argc + 5];
  int vmjcArgc = 1;
  const char** gccArgv = new const char*[argc + 32];
  int gccArgc = 1;
 
  bool runGCC = true;
  char* className = 0;

  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-shared")) {
      gccArgv[gccArgc++] = argv[i];
    } else if (!strcmp(argv[i], "-O1") || !strcmp(argv[i], "-O2") ||
               !strcmp(argv[i], "-O3")) {
      opt = argv[i];
      vmjcArgv[vmjcArgc++] = (char*)"-std-compile-opts";
    } else if (argv[i][0] == '-' && argv[i][1] == 'S') {
      runGCC = false;
    } else if (argv[i][0] == '-' && argv[i][1] == 'c') {
      gccArgv[gccArgc++] = argv[i];
    } else if (argv[i][0] == '-' && argv[i][1] == 'l') {
      gccArgv[gccArgc++] = argv[i];
    } else if (argv[i][0] == '-' && argv[i][1] == 'L') {
      gccArgv[gccArgc++] = argv[i];
    } else if (argv[i][0] == '-' && argv[i][1] == 'W') {
      gccArgv[gccArgc++] = argv[i];
    } else if (argv[i][0] == '-' && argv[i][1] == 'o') {
      gccArgv[gccArgc++] = argv[i++];
      gccArgv[gccArgc++] = argv[i];
    } else if (argv[i][0] != '-') {
      char* name = argv[i];
      int len = strlen(name);
      if (len > 4 && (!strcmp(&name[len - 4], ".jar") || 
                      !strcmp(&name[len - 4], ".zip"))) {
        vmjcArgv[vmjcArgc++] = name;
        className = strndup(name, len - 4);
      } else if (len > 6 && !strcmp(&name[len - 6], ".class")) {
        vmjcArgv[vmjcArgc++] = name;
        className = strndup(name, len - 6);
      } else {
        gccArgv[gccArgc++] = name;
      }
    } else if (!strcmp(argv[i], "--help")) {
      fprintf(stderr, "Usage: llcj [options] file ...\n"
                      "The Java to native compiler. Run vmjc --help for more "
                      "information on the real AOT compiler.\n");
      delete gccArgv;
      delete vmjcArgv;
      if (className) free(className);
      return 0;
    } else {
      vmjcArgv[vmjcArgc++] = argv[i];
    }
  } 

  vmjcArgv[vmjcArgc] = 0;
  gccArgv[gccArgc] = 0;

  std::string errMsg;
 
  const sys::Path& tempDir = SaveTemps
      ? sys::Path(sys::Path::GetCurrentDirectory())
      : sys::Path(sys::Path::GetTemporaryDirectory());

  sys::Path Out = tempDir;
  int res = 0;
  sys::Path Prog;
  
  if (!className) {
    fprintf(stderr, "No Java file specified.... Abort\n");
    goto cleanup;
  }
  
  Prog = sys::Program::FindProgramByName("vmjc");

  if (Prog.isEmpty()) {
    fprintf(stderr, "Can't find vmjc.... Abort\n");
    goto cleanup;
  }
  
  Out.appendComponent(className);
  Out.appendSuffix("bc");
  
  vmjcArgv[0] = Prog.toString().c_str();
  vmjcArgv[vmjcArgc++] = "-f";
  vmjcArgv[vmjcArgc++] = "-o";
  vmjcArgv[vmjcArgc++] = Out.toString().c_str();
 
  res = sys::Program::ExecuteAndWait(Prog, vmjcArgv);
  
  if (!res && opt) {
    sys::Path OptOut = tempDir;
    OptOut.appendComponent("llvmopt");
    OptOut.appendSuffix("bc");
    
    sys::Path Prog = sys::Program::FindProgramByName("opt");
  
    if (Prog.isEmpty()) {
      fprintf(stderr, "Can't find opt.... Abort\n");
      goto cleanup;
    }
    
    const char* optArgv[7];
    optArgv[0] = Prog.toString().c_str();
    optArgv[1] = Out.toString().c_str();
    optArgv[2] = "-f";
    optArgv[3] = "-o";
    optArgv[4] = OptOut.toString().c_str();
    if (opt) {
      optArgv[5] = opt;
      optArgv[6] = 0;
    } else {
      optArgv[5] = 0;
    }
  
    res = sys::Program::ExecuteAndWait(Prog, optArgv);
    Out = OptOut;
  }

  if (!res) {
    sys::Path LlcOut = tempDir;
    LlcOut.appendComponent(className);
    LlcOut.appendSuffix("s");
   
    sys::Path Prog = sys::Program::FindProgramByName("llc");
  
    if (Prog.isEmpty()) {
      fprintf(stderr, "Can't find llc.... Abort\n");
      goto cleanup;
    }
    
    const char* llcArgv[8];
    llcArgv[0] = Prog.toString().c_str();
    llcArgv[1] = Out.toString().c_str();
    llcArgv[2] = "-relocation-model=pic";
    llcArgv[3] = "-disable-fp-elim";
    llcArgv[4] = "-f";
    llcArgv[5] = "-o";
    llcArgv[6] = LlcOut.toString().c_str();
    llcArgv[7] = 0;
  
    res = sys::Program::ExecuteAndWait(Prog, llcArgv);
    Out = LlcOut;
  }

  if (!res && runGCC) {
    sys::Path Prog = sys::Program::FindProgramByName("g++");
  
    if (Prog.isEmpty()) {
      fprintf(stderr, "Can't find gcc.... Abort\n");
      goto cleanup;
    }

    gccArgv[0] = Prog.toString().c_str();
    gccArgv[gccArgc++] = Out.toString().c_str();
    gccArgv[gccArgc++] = LLVMLibs;
    gccArgv[gccArgc++] = VMKITLibs1;
    gccArgv[gccArgc++] = VMKITLibs2;
    gccArgv[gccArgc++] = VMKITLibs3;
    gccArgv[gccArgc++] = "-pthread";
    gccArgv[gccArgc++] = "-lgc"; // does not hurt to add it
    gccArgv[gccArgc++] = "-lm";
    gccArgv[gccArgc++] = "-ldl";
    gccArgv[gccArgc++] = "-lz";
    gccArgv[gccArgc++] = "-ljnjvm";
    gccArgv[gccArgc++] = "-lvmjc";
    gccArgv[gccArgc++] = "-lLLVMSupport";
    gccArgv[gccArgc++] = 0;

    res = sys::Program::ExecuteAndWait(Prog, gccArgv);
    
  }

cleanup:
  if (!SaveTemps) 
    tempDir.eraseFromDisk(true);
  
  delete gccArgv;
  delete vmjcArgv;
  free(className);

  return 0;
}

