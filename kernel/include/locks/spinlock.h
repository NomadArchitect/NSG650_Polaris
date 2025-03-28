#ifndef SPINLOCK_H
#define SPINLOCK_H

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

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	bool lock;
	void *last_owner;
} lock_t;

#define spinlock_init(s) \
	s.lock = 0;          \
	s.last_owner = NULL;

bool spinlock_acquire(lock_t *spin);
void spinlock_acquire_or_wait(lock_t *spin);
void spinlock_drop(lock_t *spin);

#endif
