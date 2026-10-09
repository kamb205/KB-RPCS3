/*
 * PS5 Vulkan Template - the samples linked into the title.
 *
 * One line a sample: SAMPLE(id, in the menu, title, description). With one
 * line only, the title is that program: it starts with no menu, and ends when
 * the program does (ps5/tools/new-title.py makes such titles). The build
 * (ps5/CMakeLists.txt) compiles examples/<id>/<id>.cpp for every line here, in
 * a namespace of its own, and packages shaders/glsl/<id>/. A sample goes in the
 * menu only once it has been proven on the console (ps5/README.md); until then
 * test runs reach it and the menu does not.
 *
 * A sample may have variants for test runs: VARIANTS(id, "a b c") lets a run
 * name "id.a", which starts the sample with ps5.variant "a" (the UI gallery
 * starts on that design). A run of "all" or "menu" runs a sample's variants
 * instead of the sample itself.
 *
 * Copyright (C) 2026 Mihawk
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */
#include "ps5_samples.h"

#define PS5_SAMPLES \
	SAMPLE(rpcs3ps5, true, "KB-RPCS3", "KB-RPCS3 Alpha 0.1.0")

#define SAMPLE(id, menu, title, description) \
	namespace sample_##id { VulkanExampleBase *createExample(); }
PS5_SAMPLES
#undef SAMPLE

#define SAMPLE(id, menu, title, description) { #id, title, description, menu, sample_##id::createExample },
const Ps5Sample ps5Samples[] = { PS5_SAMPLES };
#undef SAMPLE

const size_t ps5SampleCount = sizeof(ps5Samples) / sizeof(ps5Samples[0]);

#define PS5_VARIANTS

#define VARIANTS(id, names) { #id, names },
const Ps5Variants ps5Variants[] = { PS5_VARIANTS { nullptr, nullptr } };
#undef VARIANTS

const size_t ps5VariantCount = sizeof(ps5Variants) / sizeof(ps5Variants[0]) - 1;
