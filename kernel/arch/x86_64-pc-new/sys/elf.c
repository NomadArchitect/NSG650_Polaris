#include <debug/debug.h>
#include <klibc/hashmap.h>
#include <klibc/module.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <sys/elf.h>

static struct function_symbol *name_to_function = NULL;
static struct function_symbol *function_to_name = NULL;
static size_t function_table_size = 0;
bool symbol_table_initialised = false;

module_list_t modules_list = {0};

static void simple_append_name(const char *string, uint64_t address) {
	size_t index = hash(string, strlen(string)) % function_table_size;
	name_to_function[index].address = address;
	name_to_function[index].name = string;
}

const char *elf_get_name_from_function(uint64_t address) {
	if (!symbol_table_initialised)
		return "";

	address -= KERNEL_BASE;
	address += 0xffffffff80000000;

	// I HATE HASH COLLISIONS
	// I HATE HASH COLLISIONS
	for (size_t i = 0; i < function_table_size; i++) {
		if (function_to_name[i].address == address)
			return function_to_name[i].name;
	}

	return "UNKNOWN";
}

uint64_t elf_get_function_from_name(const char *string) {
	if (!symbol_table_initialised)
		return (uint64_t)NULL;

	size_t index = hash(string, strlen(string)) % function_table_size;
	uint64_t address = name_to_function[index].address;

	address -= 0xffffffff80000000;
	address += KERNEL_BASE;

	return address;
}

void elf_init_function_table(uint8_t *binary) {
	vec_init(&modules_list);

	Elf64_Ehdr *ehdr = (Elf64_Ehdr *)binary;

	Elf64_Shdr *elf_section_headers = (Elf64_Shdr *)(binary + ehdr->e_shoff);

	Elf64_Shdr *symtab = NULL;
	char *strtab = NULL;

	for (int i = 0; i < ehdr->e_shnum; i++) {
		if (elf_section_headers[i].sh_type == SHT_SYMTAB) {
			symtab = &elf_section_headers[i];
			strtab = (char *)((uintptr_t)binary +
							  elf_section_headers[symtab->sh_link].sh_offset);
			break;
		}
	}

	if (symtab != NULL) {
		Elf64_Sym *sym_entries = (Elf64_Sym *)(binary + symtab->sh_offset);
		size_t num_symbols = symtab->sh_size / symtab->sh_entsize;

		function_table_size = num_symbols;
		name_to_function =
			kmalloc(sizeof(struct function_symbol) * num_symbols);
		function_to_name =
			kmalloc(sizeof(struct function_symbol) * num_symbols);

		for (size_t i = 0; i < num_symbols; i++) {
			if (ELF64_ST_TYPE(sym_entries[i].st_info) == STT_FUNC &&
				ELF64_ST_BIND(sym_entries[i].st_info) == STB_GLOBAL) {
				char *dupped =
					strdup((char *)((uint64_t)strtab + sym_entries[i].st_name));

				// We need a better hash function for the addresses since there
				// are hash collisions. For now we are using this slow way.
				function_to_name[i].address = sym_entries[i].st_value;
				function_to_name[i].name = dupped;

				simple_append_name(dupped, sym_entries[i].st_value);
			}
		}
		symbol_table_initialised = true;
	}
}

uint64_t module_load(const uint8_t *data) {
	struct module *m = kmalloc(sizeof(struct module));
	memzero(m, sizeof(struct module));

	Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;

	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG)) {
		return 1;
	}

	if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
		ehdr->e_ident[EI_DATA] != ELFDATA2LSB || ehdr->e_ident[EI_OSABI] != 0 ||
		ehdr->e_machine != EM_X86_64) {
		return 1;
	}

	if (ehdr->e_type != 1) {
		return 1;
	}

	if (ehdr->e_shentsize != sizeof(Elf64_Shdr)) {
		return 1;
	}

	// Setup variable for the section headers
	size_t section_count = ehdr->e_shnum;
	Elf64_Shdr *elf_section_headers =
		(Elf64_Shdr *)((uintptr_t)data + ehdr->e_shoff);

	for (size_t index = 0; index < section_count; index++) {
		Elf64_Shdr *section = (elf_section_headers + index);

		// If this section should allocate memory and is bigger than 0
		if ((section->sh_flags & SHF_ALLOC) && section->sh_size > 0) {
			// Allocate memory, currenty RW so we can write to it
			char *mem = (char *)((uint64_t)pmm_allocz(
									 1 + (section->sh_size / PAGE_SIZE)) +
								 MEM_PHYS_OFFSET);

			m->mappings[m->mappings_count].addr = (uint64_t)(size_t)mem;
			m->mappings[m->mappings_count++].size = section->sh_size;

			if (section->sh_type == SHT_PROGBITS) {
				// Read data from the file
				memcpy(mem, data + section->sh_offset, section->sh_size);
			}
			section->sh_addr = (uintptr_t)mem;
		}

		// Load symbol and string tables from the file
		if (section->sh_type == SHT_SYMTAB || section->sh_type == SHT_STRTAB) {
			section->sh_addr = (uintptr_t)data + section->sh_offset;
		}
	}

	for (size_t index = 0; index < section_count; index++) {
		Elf64_Shdr *section = (elf_section_headers + index);

		if (section->sh_type == SHT_RELA) {
			size_t entry_count = section->sh_size / section->sh_entsize;
			if (section->sh_entsize != sizeof(Elf64_Rela)) {
				return 1;
			}

			// Load relocation entries from the file
			Elf64_Rela *entries =
				(Elf64_Rela *)((uintptr_t)data + section->sh_offset);
			section->sh_addr = (uint64_t)(size_t)data + section->sh_offset;

			// Locate the section we are relocating
			Elf64_Shdr *relocation_section =
				(Elf64_Shdr *)(elf_section_headers + section->sh_info);
			char *relocation_section_data = (char *)relocation_section->sh_addr;

			// Locate the symbol table for this relocation table
			Elf64_Shdr *section_symbol_table =
				(elf_section_headers + section->sh_link);
			Elf64_Sym *symbol_table =
				(Elf64_Sym *)(section_symbol_table->sh_addr);

			// Locate the string table for the symbol table
			Elf64_Shdr *section_string_table =
				(elf_section_headers + section_symbol_table->sh_link);
			char *string_table = (char *)(section_string_table->sh_addr);

			// Relocate all the entries
			for (size_t entry_ind = 0; entry_ind < entry_count; entry_ind++) {
				Elf64_Rela *entry = (entries + entry_ind);

				// Find the symbol for this entry
				Elf64_Sym *symbol = (symbol_table + ELF64_R_SYM(entry->r_info));
				char *symbol_name = (string_table + symbol->st_name);

				if (ELF64_R_TYPE(entry->r_info) == 1) {
					// Determine the offset in the section
					uint64_t *location =
						(uint64_t *)((uint64_t)relocation_section_data +
									 entry->r_offset);

					// Check that the symbol is defined in this file
					if (symbol->st_shndx > 0) {
						// Calculate the location of the symbol
						Elf64_Shdr *symbol_section =
							(elf_section_headers + symbol->st_shndx);
						uint64_t symbol_value = symbol_section->sh_addr +
												symbol->st_value +
												entry->r_addend;

						// Store the location
						*location = symbol_value;
					} else {
						// The symbol is not defined inside the object file
						// resolve using other rules
						*location = elf_get_function_from_name(symbol_name);
					}
				} else {
					return 1;
				}
			}
		}
	}

	for (size_t index = 0; index < section_count; index++) {
		Elf64_Shdr *section = (elf_section_headers + index);

		if ((section->sh_flags & SHF_ALLOC) && section->sh_size > 0) {
			// Calculate the correct permissions
			int prot = PAGE_READ;
			if (section->sh_flags & SHF_WRITE)
				prot |= PAGE_WRITE;
			if (section->sh_flags & SHF_EXECINSTR)
				prot |= PAGE_EXECUTE;

			m->mappings[index].prot = prot;

			for (size_t i = 0; i < section->sh_size; i += PAGE_SIZE) {
				vmm_map_page(kernel_pagemap, section->sh_addr,
							 section->sh_addr - MEM_PHYS_OFFSET, prot,
							 Size4KiB);
			}
		}
	}

	module_entry_t run_func = NULL;
	for (size_t index = 0; index < section_count; index++) {
		Elf64_Shdr *section = (elf_section_headers + index);

		// Look in all symbol tables for the run function
		if (section->sh_type == SHT_SYMTAB) {
			Elf64_Sym *symbol_table = (Elf64_Sym *)section->sh_addr;

			// Find the string table for this symbol table
			Elf64_Shdr *section_string_table =
				(elf_section_headers + section->sh_link);
			char *string_table = (char *)(section_string_table->sh_addr);

			size_t symbol_count = section->sh_size / section->sh_entsize;
			for (size_t i = 0; i < symbol_count; i++) {
				// Get the symbol from the table
				Elf64_Sym *symbol = (symbol_table + i);
				char *symbol_name = (string_table + symbol->st_name);
				Elf64_Shdr *run_section =
					(elf_section_headers + symbol->st_shndx);

				// Check if it is the run symbol
				if (strcmp("driver_entry", symbol_name) == 0 &&
					((symbol->st_info >> 4) & 0xf) == 1 &&
					(run_section->sh_flags & 4)) {
					// Calculate the symbol location
					run_func = (module_entry_t)(run_section->sh_addr +
												symbol->st_value);

					break;
				}
			}
		}

		if (run_func)
			break;
	}

	if (run_func != NULL) {
		vec_push(&modules_list, m);

		return run_func(m);
	} else {
		return 1;
	}
}

bool module_unload(const char *name) {
	for (int i = 0; i < modules_list.length; i++) {
		if (!strcmp(name, modules_list.data[i]->name)) {
			struct module *m = modules_list.data[i];
			if (!m->exit)
				return false;

			m->exit();
			for (size_t j = 0; j < m->mappings_count; j++) {
				pmm_free((void *)(m->mappings[j].addr - MEM_PHYS_OFFSET),
						 (m->mappings[j].size / PAGE_SIZE) + 1);
				for (size_t k = 0; k < m->mappings[j].size; k += PAGE_SIZE) {
					vmm_unmap_page(kernel_pagemap, m->mappings[j].addr);
				}
			}
			vec_remove(&modules_list, m);
			kfree(m);
			return true;
		}
	}
	return false;
}