#ifndef _MEMALLOC_H_
#define _MEMALLOC_H_

#include <stdbool.h>
#include <sys/types.h>

#include "u/vector.h"

typedef struct {
	size_t offset;
	size_t len;
	bool used;
} MemSection;

typedef struct {
	off_t dmem;
	void* mappedmem;
	size_t len;
	size_t freelen;
	UVec sections;	// MemSection
} MemMappedBlock;

typedef struct {
	size_t blocksize;
	int protection;
	int memtype;
	UVec mblocks;  // MemMappedBlock
} MemoryAllocator;

MemoryAllocator memalloc_init(size_t blocksize, int prot, int memtype);
void memalloc_destroy(MemoryAllocator* ma);

void* memalloc_alloc(MemoryAllocator* ma, size_t size, size_t alignment);
void memalloc_free(MemoryAllocator* ma, void* ptr);

#endif	// _MEMALLOC_H_
