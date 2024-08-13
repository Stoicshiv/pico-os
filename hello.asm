; hello.asm
BITS 16
ORG 0x7C00

start:
    mov ah, 0x0E ; BIOS function to print character
    mov al, 'H'
    int 0x10     ; Call BIOS interrupt
    mov al, 'e'
    int 0x10
    ; ... (repeat for other characters)
    jmp $        ; Infinite loop

times 510-($-$$) db 0
dw 0xAA55      ; Boot signature
