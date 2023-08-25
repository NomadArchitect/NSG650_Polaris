#include <cpu/cr.h>
#include <cpu/msr.h>
#include <cpu/smp.h>
#include <cpu_features.h>
#include <cpuid.h>
#include <debug/debug.h>
#include <klibc/kargs.h>
#include <klibc/mem.h>
#include <locks/spinlock.h>
#include <mm/slab.h>
#include <sys/apic.h>
#include <sys/gdt.h>
#include <sys/isr.h>
#include <sys/prcb.h>

bool is_smp = false;
static lock_t smp_lock = 0;
struct prcb *prcbs = NULL;
static uint32_t smp_bsp_lapic_id = 0;
static size_t cpu_count = 0;
static size_t initialized_cpus = 0;

extern void gdt_reload(void);
extern void idt_reload(void);

static void smp_cpu_init(struct limine_smp_info *smp_info) {
	struct prcb *prcb_local = (void *)smp_info->extra_argument;
	gdt_reload();
	idt_reload();

	spinlock_acquire_or_wait(&smp_lock);

	gdt_load_tss(&prcb_local->cpu_tss);

	set_kernel_gs(prcb_local);
	set_user_gs(prcb_local);

	// SSE/SSE2
	uint64_t cr0 = 0;
	cr0 = read_cr("0");
	cr0 &= ~(1 << 2);
	cr0 |= (1 << 1);
	write_cr("0", cr0);

	uint64_t cr4 = 0;
	cr4 = read_cr("4");
	cr4 |= (3 << 9);
	write_cr("4", cr4);
	/*
		// Enable syscall in EFER
		wrmsr(0xc0000080, rdmsr(0xc0000080) | 1);

		// Set up syscall
		wrmsr(0xc0000081, 0x13000800000000);
		// Syscall entry address
		wrmsr(0xc0000082, (uint64_t)amd_syscall_entry);
		// Flags mask
		wrmsr(0xc0000084, (uint64_t) ~((uint32_t)0x2));
	*/
	// Security features
	uint32_t a = 0, b = 0, c = 0, d = 0;
	__get_cpuid(7, &a, &b, &c, &d);
	if (b & CPUID_SMEP) {
		cr4 = read_cr("4");
		cr4 |= (1 << 20); // Enable SMEP
		write_cr("4", cr4);
	}

	if (b & CPUID_SMAP) {
		cr4 = read_cr("4");
		cr4 |= (1 << 21); // Enable SMAP
		write_cr("4", cr4);
		asm("clac");
	}

	if (c & CPUID_UMIP) {
		cr4 = read_cr("4");
		cr4 |= (1 << 11); // Enable UMIP
		write_cr("4", cr4);
	}

	__get_cpuid(1, &a, &b, &c, &d);
	if (c & bit_XSAVE) {
		// Enable XSAVE and x{get,set}bv
		cr4 = read_cr("4");
		cr4 |= (uint64_t)1 << 18;
		write_cr("4", cr4);

		uint64_t xcr0 = 0;
		xcr0 |= (uint64_t)1 << 0;
		xcr0 |= (uint64_t)1 << 1;

		if (c & bit_AVX) {
			xcr0 |= (uint64_t)1 << 2;
		}

		__get_cpuid(7, &a, &b, &c, &d);
		if (b & bit_AVX512F) {
			xcr0 |= (uint64_t)1 << 5;
			xcr0 |= (uint64_t)1 << 6;
			xcr0 |= (uint64_t)1 << 7;
		}

		wrxcr(0, xcr0);

		__cpuid(13, a, b, c, d);

		prcb_local->fpu_storage_size = c;
		prcb_local->fpu_save = xsave;
		prcb_local->fpu_restore = xrstor;
	} else {
		prcb_local->fpu_storage_size = 512;
		prcb_local->fpu_save = fxsave;
		prcb_local->fpu_restore = fxrstor;
	}

	if (prcb_local->lapic_id != smp_bsp_lapic_id) {
		lapic_init(smp_info->lapic_id);
		kprintf("CPU%u: I am alive!\n", prcb_local->cpu_number);
		initialized_cpus++;
		spinlock_drop(&smp_lock);
		sti();
		for (;;) {
			halt();
		}
	}
	kprintf("CPU%u: I am alive and I am the BSP!\n", prcb_local->cpu_number);
	initialized_cpus++;
	spinlock_drop(&smp_lock);
}

void smp_init(struct limine_smp_response *smp_info) {
	kprintf("SMP: Total number of Processors Installed %u\n",
			smp_info->cpu_count);
	smp_bsp_lapic_id = smp_info->bsp_lapic_id;

	if (kernel_arguments.cpu_count) {
		cpu_count = kernel_arguments.cpu_count;
		kprintf("SMP: Setting up only %u Processors\n", cpu_count);
	} else {
		cpu_count = smp_info->cpu_count;
	}

	prcbs = kcalloc(cpu_count, struct prcb);

	memzero(prcbs, sizeof(struct prcb) * cpu_count);

	for (int i = 0; i < cpu_count; i++) {
		struct limine_smp_info *cpu = smp_info->cpus[i];
		cpu->extra_argument = (uint64_t)&prcbs[i];
		prcbs[i].cpu_number = i;
		prcbs[i].lapic_id = cpu->lapic_id;
		prcbs[i].cpu_tss.rsp0 =
			(uint64_t)(STACK_SIZE / PAGE_SIZE) + MEM_PHYS_OFFSET;
		prcbs[i].cpu_tss.ist1 =
			(uint64_t)(STACK_SIZE / PAGE_SIZE) + MEM_PHYS_OFFSET;

		if (cpu->lapic_id != smp_bsp_lapic_id) {
			cpu->goto_address = smp_cpu_init;
		} else {
			smp_cpu_init(cpu);
		}
	}

	while (initialized_cpus != cpu_count) {
		pause();
	}

	is_smp = true;
	kprintf("SMP: %u CPUs installed in the system\n",
			prcb_return_installed_cpus());

	is_smp = true;
}

size_t prcb_return_installed_cpus(void) {
	return initialized_cpus;
}