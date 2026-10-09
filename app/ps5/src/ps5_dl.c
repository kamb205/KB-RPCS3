/*
 * RPCS3 PS5 - dlopen/dlsym/dlerror/dlclose for the emulator's JIT.
 *
 * LLVM's JIT asks for the process's own handle (dlopen(NULL)) before anything else and then
 * looks up the C runtime functions its generated code calls in it. The console refuses
 * dlopen(NULL) and its dlerror() is then NULL, which LLVM copied into a std::string: every PPU
 * compiler thread faulted on address 0 at its first module (console run 7, LIMBO). PS5 RetroArch
 * met the same with its RPCS3 core (src/core_loader_ps5.cpp, ps5_cores_dlopen). Here the
 * process handle is the title's own table of those functions; no other library is opened.
 * Bound in place of the system's by tools/link-title.sh (--defsym, kept local).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static char g_process;
static _Thread_local char g_error[192];
static _Thread_local int g_error_set;

static void set_error(const char *text, const char *name)
{
    snprintf(g_error, sizeof(g_error), "%s%s%s", name ? name : "", name ? ": " : "", text);
    g_error_set = 1;
}

/* The functions generated code may call, found by name (the addresses are the title's imports
 * and its own copies of the compiler runtime). */
#define FUNCTIONS(X) \
    X(memcpy) X(memmove) X(memset) X(memcmp) X(strlen) X(strcmp) X(strncmp) X(strcpy) \
    X(malloc) X(free) X(calloc) X(realloc) X(abort) \
    X(sin) X(cos) X(tan) X(asin) X(acos) X(atan) X(atan2) X(sinh) X(cosh) X(tanh) \
    X(exp) X(exp2) X(expm1) X(log) X(log2) X(log10) X(log1p) X(pow) X(fmod) X(sqrt) X(cbrt) X(hypot) \
    X(floor) X(ceil) X(trunc) X(round) X(rint) X(nearbyint) X(lround) X(llround) X(lrint) X(llrint) \
    X(fabs) X(fmin) X(fmax) X(fma) X(ldexp) X(frexp) X(modf) X(remainder) X(copysign) \
    X(sinf) X(cosf) X(tanf) X(asinf) X(acosf) X(atanf) X(atan2f) X(sinhf) X(coshf) X(tanhf) \
    X(expf) X(exp2f) X(expm1f) X(logf) X(log2f) X(log10f) X(log1pf) X(powf) X(fmodf) X(sqrtf) \
    X(cbrtf) X(hypotf) X(floorf) X(ceilf) X(truncf) X(roundf) X(rintf) X(nearbyintf) X(lroundf) \
    X(lrintf) X(fabsf) X(fminf) X(fmaxf) X(fmaf) X(ldexpf) X(frexpf) X(modff) X(remainderf) \
    X(copysignf) \
    X(__udivti3) X(__divti3) X(__umodti3) X(__modti3) X(__multi3) X(__ashlti3) X(__lshrti3) \
    X(__ashrti3) X(__fixdfti) X(__fixsfti) X(__fixunsdfti) X(__fixunssfti) X(__floattidf) \
    X(__floattisf) X(__floatuntidf) X(__floatuntisf) X(__truncdfhf2) X(__truncsfhf2) \
    X(__extendhfsf2) X(__gnu_h2f_ieee) X(__gnu_f2h_ieee) X(__powidf2) X(__powisf2)

#define DECLARE(name) extern char rpcs3ps5_dl_##name __asm__(#name);
FUNCTIONS(DECLARE)

static const struct { const char *name; const void *address; } g_functions[] = {
#define ENTRY(name) {#name, &rpcs3ps5_dl_##name},
    FUNCTIONS(ENTRY)
};

void *rpcs3ps5_dlopen(const char *path, int mode)
{
    (void)mode;
    if (!path)
        return &g_process;
    set_error("no library can be opened by the title", path);
    return NULL;
}

void *rpcs3ps5_dlsym(void *handle, const char *name)
{
    if (name) {
        /* The process handle, RTLD_DEFAULT (NULL) or RTLD_NEXT/RTLD_SELF (small negatives) */
        if (handle == &g_process || (size_t)handle < 16 || (size_t)handle > (size_t)-16) {
            /* LLVM may ask with the Mach-O style leading underscore */
            const char *bare = name;
            for (int pass = 0; pass < 2; pass++) {
                for (size_t i = 0; i < sizeof(g_functions) / sizeof(g_functions[0]); i++)
                    if (g_functions[i].address && strcmp(g_functions[i].name, bare) == 0)
                        return (void *)g_functions[i].address;
                if (bare[0] != '_')
                    break;
                bare++;
            }
        }
    }
    set_error("symbol not found", name);
    return NULL;
}

char *rpcs3ps5_dlerror(void)
{
    if (!g_error_set)
        return NULL;
    g_error_set = 0;
    return g_error;
}

int rpcs3ps5_dlclose(void *handle)
{
    (void)handle;
    return 0;
}
