#include <asm/asm.h>
#include <debug/debug.h>
#include <fw/acpi.h>
#include <fw/madt.h>
#include <mm/vmm.h>

bool use_xsdt;
struct rsdt *rsdt = NULL;
uint8_t revision;

void acpi_init(acpi_xsdp_t *rsdp) {
	kprintf("ACPI: Revision: %u\n", rsdp->revision);
	revision = rsdp->revision;
	if (rsdp->revision >= 2 && rsdp->xsdt) {
		use_xsdt = true;
		rsdt = (struct rsdt *)((uintptr_t)rsdp->xsdt + MEM_PHYS_OFFSET);
		kprintf("ACPI: Using XSDT at 0x%p\n", (uintptr_t)rsdt);
	} else {
		use_xsdt = false;
		rsdt = (struct rsdt *)((uintptr_t)rsdp->rsdt + MEM_PHYS_OFFSET);
		kprintf("ACPI: Using RSDT at 0x%p\n", (uintptr_t)rsdt);
	}

	madt_init();
}

// Following function based on
// https://github.com/managarm/lai/blob/master/helpers/pc-bios.c's function
// lai_bios_calc_checksum()
static uint8_t acpi_checksum(void *ptr, size_t size) {
	uint8_t sum = 0, *_ptr = ptr;
	for (size_t i = 0; i < size; i++)
		sum += _ptr[i];
	return sum;
}

void *acpi_find_sdt(const char *signature, int index) {
	int cnt = 0;

	const size_t entries =
		(rsdt->header.length - sizeof(acpi_header_t)) / (use_xsdt ? 8 : 4);

	for (size_t i = 0; i < entries; i++) {
		acpi_header_t *ptr = NULL;

		if (use_xsdt) {
			ptr =
				(acpi_header_t *)((uintptr_t)((uint64_t *)rsdt->ptrs_start)[i]);
			kprintf("0x%p\n", ((uintptr_t)((uint64_t *)rsdt->ptrs_start)));
		} else {
			ptr =
				(acpi_header_t *)((uintptr_t)((uint32_t *)rsdt->ptrs_start)[i]);
		}

		ptr = (acpi_header_t *)((uintptr_t)ptr + MEM_PHYS_OFFSET);

		if (!memcmp(ptr->signature, signature, 4) &&
			!acpi_checksum(ptr, ptr->length) && cnt++ == index) {
			return (void *)ptr;
		}
	}

	return NULL;
}