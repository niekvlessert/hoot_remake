.code16
.global _start
_start:
    push %ds
    xor %ax,%ax
    mov %ax,%ds
    movw $control, 0x01fc
    movw $0x0060, 0x01fe
    movw $timer_isr, 0x0050
    movw $0x0060, 0x0052
    pop %ds
    hlt
hang:
    jmp hang

control:
    push %ax
    push %dx
    mov $0x07e0,%dx
    in (%dx),%al
    cmp $0,%al
    jne control_done
    # SSG A tone period 0x0040, tone enabled, initially muted.
    mov $0x0188,%dx
    mov $0,%al
    out %al,(%dx)
    add $2,%dx
    mov $0x40,%al
    out %al,(%dx)
    sub $2,%dx
    mov $1,%al
    out %al,(%dx)
    add $2,%dx
    xor %al,%al
    out %al,(%dx)
    sub $2,%dx
    mov $7,%al
    out %al,(%dx)
    add $2,%dx
    mov $0x3e,%al
    out %al,(%dx)
    sub $2,%dx
    mov $8,%al
    out %al,(%dx)
    add $2,%dx
    xor %al,%al
    out %al,(%dx)
    # YM Timer B: value 200, load B + IRQ enable + status reset.
    sub $2,%dx
    mov $0x26,%al
    out %al,(%dx)
    add $2,%dx
    mov $200,%al
    out %al,(%dx)
    sub $2,%dx
    mov $0x27,%al
    out %al,(%dx)
    add $2,%dx
    mov $0x3a,%al
    out %al,(%dx)
    # PC-98 slave PIC bit 4 -> INT 14h.
    mov $0x000a,%dx
    in (%dx),%al
    and $0xef,%al
    out %al,(%dx)
control_done:
    pop %dx
    pop %ax
    iret

timer_isr:
    push %ax
    push %dx
    mov $0x0188,%dx
    mov $8,%al
    out %al,(%dx)
    add $2,%dx
    mov $0x0f,%al
    out %al,(%dx)
    sub $2,%dx
    mov $0x27,%al
    out %al,(%dx)
    add $2,%dx
    mov $0x3a,%al
    out %al,(%dx)
    pop %dx
    pop %ax
    iret
