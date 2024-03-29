#include "gui.h"

Union default_union = { .limit = SIZE_MAX };

Union *union_Default(void)
{
	return &default_union;
}

void union_Init(Union *uni, size_t limit)
{
	uni->pointers = NULL;
	uni->numPointers = 0;
	uni->limit = limit;
}

void *union_Alloc(Union *uni, size_t sz)
{
	return union_Allocf(uni, sz, 0);
}

void *union_Allocf(Union *uni, size_t sz, Uint64 flags)
{
	struct mem_ptr *ptrs;
	struct mem_ptr ptr;

	if (sz == 0) {
		return NULL;
	}

	if (uni->allocated + sz > uni->limit) {
		PRINT_DEBUG();
		fprintf(stderr, "limit exceeded old=%zu, new=%zu but limit=%zu\n",
				uni->allocated, uni->allocated + sz,
				uni->limit);
		return NULL;
	}

	ptrs = realloc(uni->pointers, sizeof(*uni->pointers) *
			(uni->numPointers + 1));
	if (ptrs == NULL) {
		PRINT_DEBUG();
		fprintf(stderr, "system error after an attempt"
				" to allocate a shadow: %s\n",
				strerror(errno));
		return NULL;
	}
	uni->pointers = ptrs;

	ptr.sys = malloc(sz);
	if (ptr.sys == NULL) {
		PRINT_DEBUG();
		fprintf(stderr, "system error after an attempt"
				" to allocate %zu bytes: %s\n",
				sz, strerror(errno));
		return NULL;
	}
	ptr.size = sz;
	ptr.flags = flags;
	uni->pointers[uni->numPointers++] = ptr;
	uni->allocated += sz;
	return ptr.sys;
}

void *union_Realloc(Union *uni, void *ptr, Size sz)
{
	struct mem_ptr *oldPtr;
	struct mem_ptr newPtr;
	Uint32 index;

	if (ptr == NULL) {
		return union_Alloc(uni, sz);
	}

	for (index = 0; index < uni->numPointers; index++) {
		if (uni->pointers[index].sys == ptr) {
			break;
		}
	}

	if (index == uni->numPointers) {
		PRINT_DEBUG();
		fprintf(stderr, "trying to reallocate non existant pointer"
				" %p to size %zu\n", ptr, sz);
		return NULL;
	}

	oldPtr = &uni->pointers[index];

	if (oldPtr->size == sz) {
		return ptr;
	}

	if (uni->allocated - oldPtr->size + sz > uni->limit) {
		PRINT_DEBUG();
		fprintf(stderr, "limit exceeded old=%zu, new=%zu but limit=%zu\n",
				uni->allocated,
				uni->allocated - oldPtr->size + sz,
				uni->limit);
		return NULL;
	}

	newPtr.sys = realloc(ptr, sz);
	if (newPtr.sys == NULL) {
		PRINT_DEBUG();
		fprintf(stderr, "system error after an attempt"
				" to reallocate %zu bytes to %zu bytes: %s\n",
				oldPtr->size, sz, strerror(errno));
		return NULL;
	}
	newPtr.size = sz;
	uni->pointers[index] = newPtr;
	uni->allocated += newPtr.size - oldPtr->size;
	return newPtr.sys;
}

void *union_Mask(Union *uni, Uint64 flags, const void *ptr)
{
	Uint32 index;

	if (ptr != NULL) {
		for (index = 0; index < uni->numPointers; index++) {
			if (uni->pointers[index].sys == ptr) {
				break;
			}
		}
	}
	for (index++; index < uni->numPointers; index++) {
		if (uni->pointers[index].flags & flags) {
			return uni->pointers[index].sys;
		}
	}
	return NULL;
}

bool union_HasPointer(Union *uni, void *ptr)
{
	for (Uint32 i = 0; i < uni->numPointers; i++) {
		if (uni->pointers[i].sys == ptr) {
			return true;
		}
	}
	return false;
}

void union_FreeAll(Union *uni)
{
	for (Uint32 i = 0; i < uni->numPointers; i++) {
		free(uni->pointers[i].sys);
	}
	free(uni->pointers);
	uni->pointers = NULL;
	uni->numPointers = 0;
	uni->allocated = 0;
}

int union_Free(Union *uni, void *ptr)
{
	Uint32 index;

	for (index = 0; index < uni->numPointers; index++) {
		if (uni->pointers[index].sys == ptr) {
			break;
		}
	}

	if (index == uni->numPointers) {
		PRINT_DEBUG();
		fprintf(stderr, "trying to free non existant pointer %p\n",
				ptr);
		return -1;
	}
	free(ptr);
	uni->allocated -= uni->pointers[index].size;
	uni->numPointers--;
	memmove(&uni->pointers[index],
			&uni->pointers[index + 1],
			sizeof(*uni->pointers) * (uni->numPointers - index));
	return 0;
}

void union_Trim(Union *uni, Uint32 numPointers)
{
	while (uni->numPointers > numPointers) {
		uni->numPointers--;
		free(uni->pointers[uni->numPointers].sys);
		uni->allocated -= uni->pointers[uni->numPointers].size;
	}
}
