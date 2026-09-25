EXTERN ResolveTestAPI:PROC
PUBLIC LoadAPI
.code
; Deliberately use every shadow-space slot and clobber all volatile argument
; registers. A conforming caller must preserve its incoming arguments itself.
LoadAPI PROC FRAME
    sub rsp, 28h
    .ALLOCSTACK 28h
    .ENDPROLOG
    mov qword ptr [rsp+30h], 111h
    mov qword ptr [rsp+38h], 222h
    mov qword ptr [rsp+40h], 333h
    mov qword ptr [rsp+48h], 444h
    call ResolveTestAPI
    mov rcx, 111h
    mov rdx, 222h
    mov r8, 333h
    mov r9, 444h
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    add rsp, 28h
    ret
LoadAPI ENDP
END
