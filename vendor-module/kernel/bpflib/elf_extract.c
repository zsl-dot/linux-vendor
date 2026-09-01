#include "elf_extract.h"
#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void *elf_extract_section(const char *path, const char *name,
			  size_t *out_size)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) { perror("open elf"); return NULL; }

	struct stat st;
	fstat(fd, &st);
	unsigned char *map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) { perror("mmap"); return NULL; }

	Elf64_Ehdr *ehdr = (Elf64_Ehdr *)map;
	Elf64_Shdr *shdrs = (Elf64_Shdr *)(map + ehdr->e_shoff);
	const char *shstrtab = (const char *)(map + shdrs[ehdr->e_shstrndx].sh_offset);

	void *data = NULL;
	for (int i = 0; i < ehdr->e_shnum; i++) {
		const char *sn = shstrtab + shdrs[i].sh_name;
		if (!strcmp(sn, name)) {
			data = malloc(shdrs[i].sh_size);
			if (data) {
				memcpy(data, map + shdrs[i].sh_offset, shdrs[i].sh_size);
				*out_size = shdrs[i].sh_size;
			}
			break;
		}
	}

	munmap(map, st.st_size);
	if (!data)
		fprintf(stderr, "Section '%s' not found in %s\n", name, path);
	return data;
}

/* Standalone: ./elf-extract <elf> <section> */
#ifndef NO_MAIN
int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <bpf_elf> <section>\n", argv[0]);
		return 1;
	}
	size_t size;
	void *data = elf_extract_section(argv[1], argv[2], &size);
	if (!data) return 1;
	fwrite(data, 1, size, stdout);
	free(data);
	return 0;
}
#endif
