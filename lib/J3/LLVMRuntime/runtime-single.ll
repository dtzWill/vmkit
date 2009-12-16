;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;; Isolate specific types ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%JavaCommonClass = type { [1 x %JavaObject*], i16,
                          %JavaClass**, i16, %UTF8*, %JavaClass*, i8*, %VT* }


%JavaClass = type { %JavaCommonClass, i32, i32, [1 x %TaskClassMirror], i8*,
                    %JavaField*, i16, %JavaField*, i16, %JavaMethod*, i16,
                    %JavaMethod*, i16, i8*, %ArrayUInt8*, i8*, %Attribut*,
                    i16, %JavaClass**, i16, %JavaClass*, i16, i8, i8, i32, i32,
                    i8*}
