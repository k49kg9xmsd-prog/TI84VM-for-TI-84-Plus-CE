	.assume adl=1
	.section .text._vm_guest_allocate,"ax",@progbits
	.global _vm_guest_allocate
	.global _vm_guest_run_asm
	.global _vm_guest_run_basic
	.global _guest_vector_ret
	.global _guest_vector_putaway
	.global _guest_vector_restore
	.extern _vm_guest_vectors
	.extern __start

; Guest execution remains inside the Flash App.
; LibLoad keeps the App's live libraries in userMem. Insert the guest before
; them; deleting asm_prgm_size here would free code the App still uses.
_vm_guest_allocate:
	push ix
	ld ix,0
	add ix,sp
	ld hl,(ix+6)
	push hl
	ld de,128
	add hl,de
	call 0x02051C
	jr c,guest_alloc_fail_pop
	pop hl
	push hl
	ld de,(0xD0118C)
	add hl,de
	ld (0xD0118C),hl
	pop hl
	ld de,0xD1A881
	call 0x020514
	ld hl,1
	pop ix
	ret
guest_alloc_fail:
	ld hl,0
	pop ix
	ret
guest_alloc_fail_pop:
	pop hl
	jr guest_alloc_fail

_vm_guest_run_asm:
	call 0x021134
	ld iy,0xD00080
	call guest_setup_vectors
	ld hl,guest_return
	push hl
	ei
	jp 0xD1A881

_vm_guest_run_basic:
	call 0x021134
	ld iy,0xD00080
	set 1,(iy+0x08)
	set 6,(iy+0x0C)
	call guest_setup_vectors
	call 0x021138
	ld hl,guest_return
	push hl
	ei
	jp 0x020F00

guest_setup_vectors:
	ld hl,_vm_guest_vectors
	call 0x020190
	ld hl,guest_error
	jp 0x020798
guest_return:
	call 0x02079C
	jr guest_finish
guest_error:
guest_finish:
	di
	ld iy,0xD00080
	res 1,(iy+0x08)
	res 6,(iy+0x0C)
	res 3,(iy+0x05)
	call 0x021DC4
	call 0x021414
	call 0x021528
	ld de,(0xD0118C)
	or a,a
	sbc hl,hl
	ld (0xD0118C),hl
	ld hl,0xD1A881
	call 0x020590
	res 4,(iy+0x34)
guest_key_release:
	call 0x02014C
	or a,a
	jr nz,guest_key_release
	xor a,a
	ld (0xD0058D),a
	; The App CRT resets the stack at __start. Doing it here as well
	; bypasses part of Cesium's return sequence for no benefit.
	jp __start

_guest_vector_ret:
	ret
_guest_vector_putaway:
	xor a,a
	ld (0xD01D0C),a
	bit 0,(iy+0x34)
	jr nz,guest_putaway_prompt
	call 0x021414
	call 0x021528
guest_putaway_prompt:
	call 0x021DC4
	call 0x02017C
	ld b,0
	ret
_guest_vector_restore:
	call 0x020828
	call 0x020814
	jp 0x020854

