/*
 * TLS Probe F - ps5_fast_emutls_get_address.
 *
 * A drop-in for compiler-rt's __emutls_get_address (libc.a / emutls.c) with one addition: a lock-free
 * cache from the thread's control-block pointer (%fs:0) to that thread's emutls_address_array, so the
 * per-access pthread_getspecific() is skipped. Same control layout, index allocation, array layout and
 * destructor semantics as compiler-rt.
 *
 * With -DTLS_F_AS_STOCK the file also defines a strong __emutls_get_address that forwards to F, so a
 * whole title can be built "production-style" with F as its TLS provider (EF2 probe).
 *
 * The cache is open-addressed with tombstones. Invariants that matter for lifetime safety:
 *   - a reader loads the key (acquire) and, only if key == its tcb, the value (acquire);
 *   - removal clears the value BEFORE tombstoning the key, so a reader that already saw the old key can
 *     never load a freed pointer;
 *   - insertion into an empty/tombstoned slot stores a NULL value before publishing the key, and an
 *     existing entry for the same tcb is updated in place (never duplicated);
 *   - only a thread's own destructor removes its entry, and it does so before freeing its array.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "emutls_control.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct emutls_address_array {
	uintptr_t skip_destructor_rounds;
	uintptr_t size; /* number of elements in data[] */
	void* data[];
} emutls_address_array;

/* compiler-rt uses malloc + over-alignment (EMUTLS_USE_POSIX_MEMALIGN == 0); keep the same layout. */
static void* f_memalign_alloc(size_t align, size_t size)
{
	if (align < sizeof(void*))
		align = sizeof(void*);
	char* object = (char*)malloc((align - 1 + sizeof(void*)) + size);
	if (!object)
		abort();
	return (void*)(((uintptr_t)(object + (align - 1 + sizeof(void*)))) & ~(uintptr_t)(align - 1));
}

static void f_memalign_free(void* base)
{
	free(((void**)base)[-1]);
}

/* ---- lock-free cache: 4096 open-addressed slots of {tcb, array*} ---- */
#define F_CACHE_SLOTS 4096u
#define F_CACHE_MASK (F_CACHE_SLOTS - 1u)
#define F_EMPTY ((uintptr_t)0)
#define F_TOMBSTONE ((uintptr_t)1)

static _Atomic uintptr_t f_cache_key[F_CACHE_SLOTS];
static _Atomic(void*) f_cache_val[F_CACHE_SLOTS];

static inline uintptr_t f_tcb(void)
{
	uintptr_t p;
	__asm__ volatile("movq %%fs:0, %0" : "=r"(p));
	return p;
}

static inline size_t f_hash(uintptr_t tcb)
{
	return (size_t)(((tcb >> 4) * 0x9E3779B97F4A7C15ull) >> 20) & F_CACHE_MASK;
}

static emutls_address_array* f_cache_lookup(uintptr_t tcb)
{
	for (size_t i = 0; i < F_CACHE_SLOTS; i++)
	{
		const size_t s = (f_hash(tcb) + i) & F_CACHE_MASK;
		const uintptr_t k = atomic_load_explicit(&f_cache_key[s], memory_order_acquire);
		if (k == F_EMPTY)
			return NULL; /* an empty slot ends the probe chain */
		if (k == tcb)
			return atomic_load_explicit(&f_cache_val[s], memory_order_acquire);
	}
	return NULL;
}

static void f_cache_insert(uintptr_t tcb, emutls_address_array* array)
{
	for (size_t i = 0; i < F_CACHE_SLOTS; i++)
	{
		const size_t s = (f_hash(tcb) + i) & F_CACHE_MASK;
		const uintptr_t k = atomic_load_explicit(&f_cache_key[s], memory_order_relaxed);
		if (k == tcb)
		{
			/* Existing entry for this thread: update it (never a duplicate). */
			atomic_store_explicit(&f_cache_val[s], array, memory_order_release);
			return;
		}
		if (k == F_EMPTY || k == F_TOMBSTONE)
		{
			/* Publish the value as NULL before claiming the slot, so no reader can observe a stale
			 * pointer from a previously tombstoned entry. */
			atomic_store_explicit(&f_cache_val[s], NULL, memory_order_relaxed);
			uintptr_t expected = k;
			if (atomic_compare_exchange_strong_explicit(&f_cache_key[s], &expected, tcb, memory_order_acq_rel, memory_order_relaxed))
			{
				atomic_store_explicit(&f_cache_val[s], array, memory_order_release);
				return;
			}
			i--; /* lost the race: retry this slot */
		}
	}
	/* Table full (4096 live threads): the cache is an optimisation; run without it. */
}

static void f_cache_remove(uintptr_t tcb)
{
	for (size_t i = 0; i < F_CACHE_SLOTS; i++)
	{
		const size_t s = (f_hash(tcb) + i) & F_CACHE_MASK;
		const uintptr_t k = atomic_load_explicit(&f_cache_key[s], memory_order_relaxed);
		if (k == F_EMPTY)
			return;
		if (k == tcb)
		{
			/* Clear the value BEFORE tombstoning the key: a reader that already loaded key == tcb must
			 * load NULL, not the pointer we are about to free. */
			atomic_store_explicit(&f_cache_val[s], NULL, memory_order_release);
			atomic_store_explicit(&f_cache_key[s], F_TOMBSTONE, memory_order_release);
			return;
		}
	}
}

/* ---- index allocation, under our own mutex (compiler-rt's statics are private to libc.a) ---- */
static pthread_mutex_t f_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t f_key;
static pthread_once_t f_once = PTHREAD_ONCE_INIT;
static size_t f_num_object = 0;

static void f_key_destructor(void* ptr)
{
	emutls_address_array* array = (emutls_address_array*)ptr;
	if (array->skip_destructor_rounds > 0)
	{
		array->skip_destructor_rounds--;
		pthread_setspecific(f_key, array);
		return;
	}
	/* Remove the cache entry BEFORE freeing the array. */
	f_cache_remove(f_tcb());
	for (uintptr_t i = 0; i < array->size; i++)
		if (array->data[i])
			f_memalign_free(array->data[i]);
	free(array);
}

static void f_init(void)
{
	if (pthread_key_create(&f_key, f_key_destructor) != 0)
		abort();
}

static uintptr_t f_get_index(__emutls_control* control)
{
	uintptr_t index = atomic_load_explicit((_Atomic uintptr_t*)&control->object.index, memory_order_acquire);
	if (!index)
	{
		pthread_once(&f_once, f_init);
		pthread_mutex_lock(&f_mutex);
		index = control->object.index;
		if (!index)
		{
			index = ++f_num_object;
			atomic_store_explicit((_Atomic uintptr_t*)&control->object.index, index, memory_order_release);
		}
		pthread_mutex_unlock(&f_mutex);
	}
	return index;
}

static inline uintptr_t f_new_size(uintptr_t index)
{
	const uintptr_t header_words = sizeof(emutls_address_array) / sizeof(void*);
	return ((index + header_words + 15) & ~(uintptr_t)15) - header_words;
}

static inline uintptr_t f_asize(uintptr_t n)
{
	return n * sizeof(void*) + sizeof(emutls_address_array);
}

static void* f_allocate_object(__emutls_control* control)
{
	size_t size = control->size;
	size_t align = control->align;
	if (align < sizeof(void*))
		align = sizeof(void*);
	if ((align & (align - 1)) != 0)
		abort();
	void* base = f_memalign_alloc(align, size);
	if (control->value)
		memcpy(base, control->value, size);
	else
		memset(base, 0, size);
	return base;
}

static emutls_address_array* f_get_array(uintptr_t index)
{
	const uintptr_t tcb = f_tcb();
	emutls_address_array* array = f_cache_lookup(tcb);
	if ((array == NULL) || (index > array->size))
	{
		if (array == NULL)
		{
			array = (emutls_address_array*)pthread_getspecific(f_key);
		}
		if (array == NULL)
		{
			const uintptr_t new_size = f_new_size(index);
			array = (emutls_address_array*)malloc(f_asize(new_size));
			if (!array)
				abort();
			memset(array->data, 0, new_size * sizeof(void*));
			array->skip_destructor_rounds = 0;
			array->size = new_size;
			if (pthread_setspecific(f_key, array) != 0)
				abort();
		}
		else if (index > array->size)
		{
			const uintptr_t orig_size = array->size;
			const uintptr_t new_size = f_new_size(index);
			emutls_address_array* grown = (emutls_address_array*)realloc(array, f_asize(new_size));
			if (!grown)
				abort(); /* keep the old array reachable on failure: abort rather than leak/corrupt */
			array = grown;
			memset(array->data + orig_size, 0, (new_size - orig_size) * sizeof(void*));
			array->size = new_size;
			if (pthread_setspecific(f_key, array) != 0)
				abort();
		}
		f_cache_insert(tcb, array);
	}
	return array;
}

void* ps5_fast_emutls_get_address(__emutls_control* control)
{
	const uintptr_t index = f_get_index(control) - 1;
	emutls_address_array* array = f_get_array(index + 1);
	if (array->data[index] == NULL)
		array->data[index] = f_allocate_object(control);
	return array->data[index];
}

/* Diagnostic for the probe: how many live (non-empty, non-tombstone, non-NULL) cache entries exist. */
unsigned ps5_fast_emutls_cache_live(void)
{
	unsigned n = 0;
	for (size_t s = 0; s < F_CACHE_SLOTS; s++)
	{
		const uintptr_t k = atomic_load_explicit(&f_cache_key[s], memory_order_relaxed);
		if (k != F_EMPTY && k != F_TOMBSTONE && atomic_load_explicit(&f_cache_val[s], memory_order_relaxed) != NULL)
			n++;
	}
	return n;
}

#ifdef TLS_F_AS_STOCK
/* Production-style: F is the title's TLS provider. Strong, so it overrides libc.a's weak stock one. */
void* __emutls_get_address(__emutls_control* control)
{
	return ps5_fast_emutls_get_address(control);
}
#endif
