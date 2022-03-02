/*
 * Copyright 2021, 2022 NSG650
 * Copyright 2021, 2022 Sebastian
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
#include <locks/spinlock.h>

bool spinlock_acquire(lock_t spin) {
	return __sync_bool_compare_and_swap(&spin, 0, 1);
}

void spinlock_acquire_or_wait(lock_t spin) {
	while (!__sync_bool_compare_and_swap(&spin, 0, 1)) {
		while (spin)
			pause();
	}
}

void spinlock_drop(lock_t spin) {
	__sync_lock_release(&spin);
}

// liballoc

lock_t lib_lock;

int liballoc_lock() {
	spinlock_acquire(lib_lock);
	cli();
	return 0;
}

int liballoc_unlock() {
	spinlock_drop(lib_lock);
	sti();
	return 0;
}
