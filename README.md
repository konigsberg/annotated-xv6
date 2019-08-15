# annotated xv6

### vm

首先内核空间中虚拟地址到物理地址的映射是直接的，不需要通过两级页表机制。

`mmu.h`中定义了一些与地址操作有关的宏：

```c
#define PDXSHIFT        22      // offset of PDX in a linear address
#define PDX(va)         (((uint)(va) >> PDXSHIFT) & 0x3FF)
```

PDX是将虚拟地址的最高10位取出来。

```c
#define PTXSHIFT        12      // offset of PTX in a linear address
#define PTX(va)         (((uint)(va) >> PTXSHIFT) & 0x3FF)
```

PTX是将虚拟地址的中间19位取出来。

```c
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))
```

PGROUNDUP将一个字节数向上取整到页的整数倍，PGROUNDDOWN将一个地址向下对齐到页地址。

```c
// Page table/directory entry flags.
#define PTE_P           0x001   // Present
#define PTE_W           0x002   // Writeable
#define PTE_U           0x004   // User
#define PTE_PS          0x080   // Page Size
```

这些是PDE和PTE的最低12个字节中的标志位。

```c
#define PTE_ADDR(pte)   ((uint)(pte) & ~0xFFF)
```

PTE_ADDR取出一个PTE/PDE的高20个字节，并将低12个字节设置为0。这是一个PTE/PDE在相应表中的索引或地址。

```c
#define PTE_FLAGS(pte)  ((uint)(pte) &  0xFFF)
```

PTE_FLAGS取出一个PTE/PDE的低12个字节，这是一个PTE/PDE的标志位。

```c
typedef uint pte_t;
```

PTE的类型是一个无符号整数。

大部分虚拟地址的操作定义在`vm.c`文件中。

在介绍虚拟内存的操作之前，先了解内核分配内存的机制，这些定义在`kalloc.c`中。

```c
struct run { 
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;
```

run是一个单链表结构，每一个run实际上是指向下一个run的地址。

```c
void
kfree(char *v)
{
  struct run *r;
  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");
  memset(v, 1, PGSIZE);
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}
```

如果v指向一块之前通过kalloc分配的内存，则kfree释放v指向的物理内存；如果v从未分配过，则把v标记为一块可用的内存。L5是说kfree是按页释放的，v必须是页对齐的。freelist是一串可用的内存页，freelist指针指向这一串内存页的开头元素。所以这里v是一块可用的页内存，L7先用1填满。L8～9确保没有同时进行的kfree操作，因为多线程情况下对freelist操作是不安全的。L11~12是把v这一页加到当前freelist的开头，先是让r的next指向旧的freelist开头，再让新的freelist指向r。这样就把一块充满了1的页内存加到了内核的freelist里。

```c
void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
```

有了对kfree的理解，freerange就很简单了。内存释放和内存初始化是紧密结合的概念。对于释放或初始化vstart到vend这一块内存，只需找到这范围内的所有的页，然后一页页初始化就可以。

```c
char*
kalloc(void)
{
  struct run *r;
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}
```

kalloc分配一页4096个字节的物理内存，实际上非常简单，就是把freelist第一页返回，同时让freelist指向下一页。如果freelist是空指针，说明freelist用尽，就返回0 。

```c
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde; 
  pte_t *pgtab;
  pde = &pgdir[PDX(va)]; 
  if(*pde & PTE_P){ 
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    memset(pgtab, 0, PGSIZE);
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}
```

walkpgdir函数在一个page directory中找到一个虚拟地址的PTE地址，如果不存在则分配该PTE。由于page directory的索引是虚拟地址的高10位，因此L6在page directory中找到了该虚拟地址应当存在的page table的地址，L7取得该地址上的内容PTE，并且判断PTE的present bit是否设置。如果设置了present bit说明该page table是真实存在的，因此在L8取出PDE的高20位，然后用了一个奇怪的映射找到了相应page table的地址。这个映射没看懂。如果没有设置present bit，则这个page table不存在，倘若不需要分配或者分配失败，则直接返回0；否则将获取的PTE的内容初始化为0（这里我没看懂为什么memset的长度是PGSIZE个字节，我认为这里是把整个page table的所有PTE都初始化为0，因此memset的长度应当是page table包含的PTE的个数），然后根据新分配的page table更新原来page directory中的PTE。最后在该page table中用虚拟地址的中间10位作为索引，找到相应的PTE。

```c
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;
  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0) 
      return -1;
    if(*pte & PTE_P) 
      panic("remap");
    *pte = pa | perm | PTE_P; 
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}
```

mappages函数将一段虚拟内存空间映射到物理地址上，如果相应的PDE或PTE不存在，则都通过walkpgdir去创建。物理地址pa应当是页对齐的。虚拟内存开始于va，长度为size，这段地址位于一块连续页中，因此先找到第一页的起始地址a和最后一页的起始地址last，然后一页页地建立映射，a每次指向页的起始地址，调用walkpgdir新分配PTE，PTE获得失败、PTE的present bit已置位都是错误的情况。然后设置PTE的标志位。

```c
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};
```

哈哈。

```c
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;
  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}
```

setupkvm建立内核的虚拟地址空间。首先创建一个page directory，初始化PGSIZE个PDE的内存，然后按照kmap中四部分数据段的定义，创建这四段数据的全部PDE和PTE。这个函数执行完后，kmap定义的四个数据段中的每个虚拟地址都可以通过两级页表找到物理地址。

```c
// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;
  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}
```

