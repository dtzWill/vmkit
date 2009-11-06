%Vector = type {i8*, i8*, i8*}
%BumpPtrAllocator = type { i32, i8*, i8*, i8*, i8*, i8*, i8*, i8* }

;;; Field 0: the VT of threads
;;; Field 1: next
;;; Field 2: prev
;;; Field 3: IsolateID
;;; Field 4: MyVM
;;; Field 5: baseSP
;;; Field 6: doYield
;;; Field 7: inGC
;;; Field 8: stackScanned
;;; Field 9: lastSP
;;; Field 10: internalThreadID
;;; field 11: routine
;;; field 12: addresses
;;; field 13: allocator
;;; field 14: MutatorContext
;;; field 15: CollectorContext
%MutatorThread = type { %VT*, %JavaThread*, %JavaThread*, i8*, i8*, i8*, i1, i1,
                        i1, i8*, i8*, i8*, %Vector, %BumpPtrAllocator, i8*, i8*}
