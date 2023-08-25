/*
 * Copyright 2021 - 2023 NSG650
 * Copyright 2021 - 2023 Neptune
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <asm/asm.h>
#include <debug/debug.h>
#include <fb/fb.h>
#include <klibc/mem.h>
#include <locks/spinlock.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/halt.h>
#ifdef SMP_READY
#include <sys/prcb.h>
#include <sys/timer.h>
#endif

static lock_t write_lock = 0;
bool in_panic = false;
bool put_to_fb = true;
bool print_now = false;

void kputchar(char c) {
	if (c == '\n')
		kputchar('\r');
	kputchar_(c);

	if (put_to_fb)
		framebuffer_putchar(c);
}

void kputs(char *string) {
	kputs_(string);

	if (put_to_fb)
		framebuffer_puts(string);
}

void syscall_puts(struct syscall_arguments *args) {
	spinlock_acquire_or_wait(&write_lock);
	if (args->args0)
		kputs((char *)args->args0);
	spinlock_drop(&write_lock);
}

static void kprintf_(char *fmt, va_list args) {
	spinlock_acquire_or_wait(&write_lock);
	if (!print_now) {
		return;
		spinlock_drop(&write_lock);
	}
#ifdef SMP_READY
	if (in_panic) {
		kputs("*** PANIC:\t");
	} else {
		uint64_t timer_tick = 0;
		if (timer_installed()) {
			timer_tick = timer_count();
		}
		char string[21] = {0};
		ltoa(timer_tick, string, 10);
		kputs("[");
		kputs(string);
		kputs("] ");
	}
#endif
	for (;;) {
		while (*fmt && *fmt != '%')
			kputchar(*fmt++);

		if (!*fmt++) {
			spinlock_drop(&write_lock);
			return;
		}

		switch (*fmt++) {
			case '%':
				kputchar('%');
				break;
			case 's': {
				char *str = (char *)va_arg(args, const char *);
				if (!str)
					kputs("(null)");
				else
					kputs(str);
				break;
			}
			case 'S': {
				char *str = (char *)va_arg(args, const char *);
				size_t len = va_arg(args, size_t);
				if (!str)
					kputs("(null)");
				else
					for (size_t i = 0; i < len; i++)
						kputchar(str[i]);
				break;
			}
			case 'x':
			case 'p': {
				char string[20] = {0};
				uint64_t number = va_arg(args, size_t);
				ultoa(number, string, 16);
				kputs(string);
				break;
			}
			case 'i':
			case 'd': {
				char string[21] = {0};
				uint64_t number = va_arg(args, size_t);
				ltoa(number, string, 10);
				kputs(string);
				break;
			}
			case 'u': {
				char string[21] = {0};
				uint64_t number = va_arg(args, size_t);
				ultoa(number, string, 10);
				kputs(string);
				break;
			}
			case 'c':
				kputchar((char)va_arg(args, int));
				break;
			default:
				kputchar('?');
				break;
		}
	}
	spinlock_drop(&write_lock);
}

void kprintffos(bool fos, char *fmt, ...) {
	if (!fos)
		put_to_fb = 0;
	va_list args;
	va_start(args, fmt);
	kprintf_(fmt, args);
	va_end(args);
}

void panic_(size_t *ip, size_t *bp, char *fmt, ...) {
	cli();
	put_to_fb = 1;
#ifdef SMP_READY
	extern bool is_smp;
#endif
	if (in_panic) {
#ifdef SMP_READY
		halt_other_cpus();
		halt_current_cpu();
#endif
	}
	in_panic = true;
#ifdef SMP_READY
	if (is_smp)
		kprintf("Panic called on CPU%d\n",
				prcb_return_current_cpu()->cpu_number);
	halt_other_cpus();
#endif
	va_list args;
	va_start(args, fmt);
	kprintf_(fmt, args);
	va_end(args);
	kprintf("Crashed at 0x%p\n", ip);
	kprintf("Stack trace:\n");
	backtrace(bp);
	halt_current_cpu();
	__builtin_unreachable();
}
