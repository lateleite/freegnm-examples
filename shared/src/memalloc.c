#include "memalloc.h"

#include <stdint.h>
#include <stdio.h>

#include <orbis/libkernel.h>

MemoryAllocator memalloc_init(size_t blocksize, int prot, int memtype) {
	MemoryAllocator res = {
		.mblocks = uvalloc(sizeof(MemMappedBlock), 0),
		.blocksize = blocksize,
		.protection = prot,
		.memtype = memtype,
	};
	return res;
}

void memalloc_destroy(MemoryAllocator* ma) {
	assert(ma);

	for (size_t i = 0; i < uvlen(&ma->mblocks); i += 1) {
		MemMappedBlock* mb = uvdata(&ma->mblocks, i);
		sceKernelReleaseDirectMemory(mb->dmem, mb->len);
		uvfree(&mb->sections);
	}
}

static MemSection* takevmem(MemMappedBlock* mb, size_t size, size_t alignment) {
	const size_t alignedsize = (size + alignment - 1) / alignment * alignment;

	// try to reuse an empty reserved section
	for (size_t i = 0; i < uvlen(&mb->sections); i += 1) {
		MemSection* s = uvdata(&mb->sections, i);
		if (!s->used && s->len >= alignedsize) {
			s->used = true;
			return s;
		}
	}

	// try to reserve a new section
	if (mb->freelen >= alignedsize) {
		MemSection* lastsec = NULL;
		size_t startoff = 0;
		const size_t numsecs = uvlen(&mb->sections);
		if (numsecs > 0) {
			lastsec = uvdata(&mb->sections, numsecs - 1);
			startoff = lastsec->offset + lastsec->len;
		}

		const size_t alignedoff =
			(startoff + alignment - 1) / alignment * alignment;
		const size_t offdiff = alignedoff - startoff;
		if (mb->len >= alignedoff + alignedsize) {
			mb->freelen -= offdiff + alignedsize;

			// add misaligned bytes to the previous section if possible
			if (lastsec) {
				lastsec->len += offdiff;
			}

			MemSection newsec = {
				.offset = alignedoff,
				.len = alignedsize,
				.used = true,
			};
			uvappend(&mb->sections, &newsec);

			return uvdata(&mb->sections, numsecs);
		}
	}

	// mapped memory does not have enough size
	return NULL;
}

void* memalloc_alloc(MemoryAllocator* ma, size_t size, size_t alignment) {
	assert(ma);
	assert(size > 0);
	assert(alignment > 0);

	// probe all memory blocks for available memory
	for (size_t i = 0; i < uvlen(&ma->mblocks); i += 1) {
		MemMappedBlock* mb = uvdata(&ma->mblocks, i);
		MemSection* s = takevmem(mb, size, alignment);
		if (s) {
			return (void*)((uintptr_t)mb->mappedmem + s->offset);
		}
	}

	// no available memory was found,
	// try to reserve memory and map it

	// direct memory's size must be a multiple of 16kB and power of 2
	// the same applies to the alignment
	const size_t blockalign = 64 * 1024;  // 64 kB
	const size_t blocklen =
		(ma->blocksize + blockalign - 1) / blockalign * blockalign;
	MemMappedBlock newmb = {
		.len = blocklen,
		.freelen = blocklen,
		.sections = uvalloc(sizeof(MemSection), 0),
	};

	int res = sceKernelAllocateDirectMemory(
		0, sceKernelGetDirectMemorySize(), blocklen, blockalign, ma->memtype,
		&newmb.dmem
	);
	if (res != 0) {
		printf("mem: sceKernelAllocateDirectMemory failed with %i\n", res);
		return NULL;
	}

	res = sceKernelMapDirectMemory(
		&newmb.mappedmem, newmb.len, ma->protection, 0, newmb.dmem, blockalign
	);
	if (res != 0) {
		printf("mem: sceKernelMapDirectMemory failed with %i\n", res);
		sceKernelReleaseDirectMemory(newmb.dmem, newmb.len);
		return false;
	}

	uvappend(&ma->mblocks, &newmb);
	MemMappedBlock* newmbptr = uvdata(&ma->mblocks, uvlen(&ma->mblocks) - 1);

	// now take virtual memory from the allocated memory
	MemSection* s = takevmem(newmbptr, size, alignment);
	if (!s) {
		puts("mem: failed to take memory from new allocated memory");
		return NULL;
	}
	return (void*)((uintptr_t)newmbptr->mappedmem + s->offset);
}

void memalloc_free(MemoryAllocator* ma, void* ptr) {
	assert(ma);
	assert(ptr);

	for (size_t i = 0; i < uvlen(&ma->mblocks); i += 1) {
		MemMappedBlock* mb = uvdata(&ma->mblocks, i);

		// check if ptr is within range
		if (!((uintptr_t)mb->mappedmem >= (uintptr_t)ptr &&
			  (uintptr_t)ptr <= ((uintptr_t)mb->mappedmem + mb->len))) {
			continue;
		}

		const uintptr_t targetoff = (uintptr_t)ptr - (uintptr_t)mb->mappedmem;

		// get its corresponding section
		for (size_t y = 0; y < uvlen(&mb->sections); y += 1) {
			MemSection* s = uvdata(&mb->sections, y);
			if (s->offset == targetoff) {
				// mark section as empty
				s->used = false;
				return;
			}
		}
	}
}
