#ifndef TIMER_H
#define TIMER_H

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

#include <stdbool.h>
#include <stdint.h>

bool timer_installed(void);
void timer_sleep(uint64_t ms);
uint64_t timer_count(void);
uint64_t timer_sched_tick(void);
void timer_stop_sched(void);
void timer_sleep_ns(uint64_t ns);
uint64_t timer_get_sleep_ns(uint64_t ns);
uint64_t timer_get_abs_count(void);

#if defined(__x86_64__)
void timer_sched_oneshot(uint8_t isr, uint32_t us);
#endif

struct __kernel_timespec {
	long long tv_sec;  /* seconds */
	long long tv_nsec; /* nanoseconds */
};

#endif
