OPTION CASEMAP:NONE
EXTERN MegaMixCameraBridge_HookedUpImpl:PROC
EXTERN MegaMixCameraBridge_HookedPvRollImpl:PROC
PUBLIC MegaMixCameraBridge_HookedUpStub
PUBLIC MegaMixCameraBridge_HookedPvRollStub
.code
; Legacy wrapper for the game's 0x1402FB7B0 setter.
; v61 does NOT install this hook; it remains here only for ABI compatibility.
; RCX = enabled byte, RDX = pointer to 3 floats.
; Preserve the original caller-visible registers and reproduce the native
; return-side values (XMM0/EAX) from the original input pointer.
MegaMixCameraBridge_HookedUpStub PROC
    push rdx
    sub rsp,20h
    ; Original RDX is now at [rsp+28h]. RCX is untouched.
    mov rdx,[rsp+28h]
    call MegaMixCameraBridge_HookedUpImpl
    mov rdx,[rsp+28h]
    add rsp,20h
    pop rdx
    movsd xmm0,qword ptr [rdx]
    mov eax,dword ptr [rdx+8]
    ret
MegaMixCameraBridge_HookedUpStub ENDP
; Called from FUN_1402FAE00 in place of the 5-byte CALL to 0x2FB7A0.
; XMM0 contains the A3DA roll in degrees. Preserve R8 because the caller
; uses R8 again immediately after the original tiny setter returns.
MegaMixCameraBridge_HookedPvRollStub PROC
    push r8
    sub rsp,20h
    call MegaMixCameraBridge_HookedPvRollImpl
    add rsp,20h
    pop r8
    ret
MegaMixCameraBridge_HookedPvRollStub ENDP
END
