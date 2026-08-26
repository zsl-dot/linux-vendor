/*
 * elf_extract.h — Extract a named section from a BPF ELF file.
 */
#ifndef ELF_EXTRACT_H
#define ELF_EXTRACT_H

#include <stddef.h>

/* Extract section `name` from ELF file at `path`.
 * Returns section data pointer (caller frees), sets *out_size. */
void *elf_extract_section(const char *path, const char *name,
			  size_t *out_size);

#endif
