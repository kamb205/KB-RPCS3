/*
 * The emulated-TLS control block, matching compiler-rt's libc/emutls.c.
 * gcc_word is mode(word): 64-bit on x86-64, so {size, align, object.index/address, value}.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __emutls_control {
	uintptr_t size;  /* size of the object in bytes */
	uintptr_t align; /* alignment of the object in bytes */
	union {
		uintptr_t index; /* 1-based; data[index-1] is the object address */
		void* address;   /* the object address, single-threaded */
	} object;
	const void* value; /* NULL, or the initial value */
} __emutls_control;

/* E: the stock compiler-rt entry point (libc.a) */
void* __emutls_get_address(__emutls_control* control);
/* F: the probe's lock-free-cache entry point */
void* ps5_fast_emutls_get_address(__emutls_control* control);

#ifdef __cplusplus
}
#endif
