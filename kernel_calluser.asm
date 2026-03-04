.data
ALIGN 16
PUBLIC g_kvas_enabled
PUBLIC g_kvashadow_offset
g_kvas_enabled      dd 0
g_kvashadow_offset  dd 0

.code

; naked float __KiCallUserMode2(pv64 OutVarPtr, pv64 CallCtx, u64 KStackControl)
; Parameters:
;   RCX = OutVarPtr
;   RDX = CallCtx
;   R8  = KStackControl

__KiCallUserMode2 PROC
    ; alloc stack
    sub rsp, 138h

    ; save non volatile float regs
	lea rax, [rsp + 100h]
    movaps xmmword ptr [rsp + 30h], xmm6
    movaps xmmword ptr [rsp + 40h], xmm7
    movaps xmmword ptr [rsp + 50h], xmm8
    movaps xmmword ptr [rsp + 60h], xmm9
    movaps xmmword ptr [rsp + 70h], xmm10
    movaps xmmword ptr [rax - 80h], xmm11
    movaps xmmword ptr [rax - 70h], xmm12
    movaps xmmword ptr [rax - 60h], xmm13
    movaps xmmword ptr [rax - 50h], xmm14
    movaps xmmword ptr [rax - 40h], xmm15

    ; save non volatile int regs
    mov [rax - 8], rbp
    mov rbp, rsp
    mov [rax], rbx
    mov [rax + 8], rdi
    mov [rax + 10h], rsi
    mov [rax + 18h], r12
    mov [rax + 20h], r13
    mov [rax + 28h], r14
    mov [rax + 30h], r15

    ; save ret val vars
    mov [rbp + 0D8h], rcx
	mov [rbp + 0E0h], rdx
    mov rbx, gs:[188h]
    mov [r8 + 20h], rsp
    mov rsi, [rbx + 90h]
    mov [rbp + 0D0h], rsi

    ; save new stack vars
    cli
    mov [rbx + 28h], r8
    mov [rbx + 38h], r9
    
    ; KVAS conditional branch
    cmp dword ptr [g_kvas_enabled], 0
    je use_legacy_path

use_kvas_path:
    ; KVAS enabled: use dynamic offset
    mov eax, [g_kvashadow_offset]
    mov gs:[rax], r8
    jmp continue_setup

use_legacy_path:
    ; Legacy path: use fixed gs:[8]
    mov rdi, gs:[8]
    mov [rdi + 4], r8

continue_setup:
    ; save cur trap frame  KeKernelStackSize = 6000h
    mov ecx, 6000h
    sub r9, rcx
    mov gs:[1A8h], r8
    mov [rbx + 30h], r9
    lea rsp, [r8 - 190h]
    mov rdi, rsp
    mov ecx, 32h
    rep movsq

    ; fix csr
    lea rbp, [rsi - 110h]
    ldmxcsr dword ptr [rbp - 54h]

    ; restore rbp, r11
    mov r11, [rbp + 0F8h]
    mov rbp, [rbp + 0D8h]

    ; load rip, rsp, rax
    mov rax, [rdx + 10h]
    mov rsp, [rdx + 8]
    mov rcx, [rdx]

    ; load floats
    movss xmm0, dword ptr [rdx + 18h]
    movss xmm2, dword ptr [rdx + 28h]
    movss xmm1, dword ptr [rdx + 20h]
    movss xmm3, dword ptr [rdx + 30h]

    ; load userctx x32
    cmp qword ptr [rdx + 38h], 0
    jz Sw2UserMode
    mov r13, [rdx + 38h]

Sw2UserMode:
    swapgs
    sysretq
__KiCallUserMode2 ENDP

END
