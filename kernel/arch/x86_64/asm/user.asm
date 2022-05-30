section .text

global user_thread

.die:
	jmp .die

user_thread:
	xor rax, rax
	mov rdi, 0x48
	int 0x80
	mov rdi, 0x65
	int 0x80
	mov rdi, 0x6c
	int 0x80
	mov rdi, 0x6c
	int 0x80
	mov rdi, 0x6f
	int 0x80
	mov rdi, 0x20
	int 0x80
	mov rdi, 0x66
	int 0x80
	mov rdi, 0x72
	int 0x80
	mov rdi, 0x6f
	int 0x80
	mov rdi, 0x6d
	int 0x80
	mov rdi, 0x20
	int 0x80
	mov rdi, 0x75
	int 0x80
	mov rdi, 0x73
	int 0x80
	mov rdi, 0x65
	int 0x80
	mov rdi, 0x72
	int 0x80
	mov rdi, 0x21
	int 0x80
	mov rdi, 0xa
	int 0x80
	jmp .die