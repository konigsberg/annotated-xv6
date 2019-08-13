// Memory layout

#define EXTMEM  0x100000            // Start of extended memory
#define PHYSTOP 0xE000000           // Top physical memory
#define DEVSPACE 0xFE000000         // Other devices are at high addresses

// Key addresses for address space layout (see kmap in vm.c for layout)
#define KERNBASE 0x80000000         // First kernel virtual address
#define KERNLINK (KERNBASE+EXTMEM)  // Address where kernel is linked
/// kerner is linked at KERNBASE:KERNBASE+KERNLINK

/// V2P and P2V are for kernel's address translation, which adopts a simple method instead of
/// page table mechanism. Kernel memory is at high virtual address but low physical address.
/// User's address is translated using page table.
/// Kernel's virtual address to physical address translation is:
/// KERNBASE:KERNBASE+PHYSTOP -> 0:PHYSTOP, so we have the following V2P and P2A macros.
#define V2P(a) (((uint) (a)) - KERNBASE)
#define P2V(a) ((void *)(((char *) (a)) + KERNBASE))
/// The simplicity of translation is for two reasons: (1) in this way, kernel can use its own 
/// instruction and data, and (2) the kernel sometimes needs to be able to write a given
/// page of physical memory.

#define V2P_WO(x) ((x) - KERNBASE)    // same as V2P, but without casts
#define P2V_WO(x) ((x) + KERNBASE)    // same as P2V, but without casts
