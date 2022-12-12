#include <types.h>
#include <spinlock.h>
#include <vm.h>

#define INVALID -1

struct coremap* cmp;                
struct spinlock cmp_spinlock;   // spinlock for the coremap array
int ram_pages;                  
vaddr_t cmp_end;               // end of coremap
bool vm_boot;                  // indicates if vm_bootstrap has completed


void vm_bootstrap(void) {
    int cmap_ppages;
    paddr_t ram_end;
    paddr_t ram_start;

    spinlock_init(&cmp_spinlock);

    ram_end = ram_getsize();
    ram_start = ram_getfirstfree();
    ram_pages = (ram_end - ram_start) / PAGE_SIZE;

    cmap_ppages = (sizeof(struct coremap) * ram_pages) / PAGE_SIZE;
    if ((sizeof(struct coremap)*ram_pages) % PAGE_SIZE != 0) {
        cmap_ppages++;
    }
    ram_pages = ram_pages - cmap_ppages;

    cmp = (struct coremap*)PADDR_TO_KVADDR(ram_start);
    cmp_end = (vaddr_t)cmp + cmap_ppages * PAGE_SIZE;
    
    // initialize coremap
    for (int i = 0; i < ram_pages; i++) {
        cmp[i].kvaddr = cmp_end + i * PAGE_SIZE;
        cmp[i].free = true;
        cmp[i].num_pages = 0;
    }

    vm_boot = true;
}


int vm_fault(int faulttype, vaddr_t faultaddress) {
    (void)faulttype;
    (void)faultaddress;
    return 0;
}


vaddr_t alloc_kpages(unsigned npages) {
    int free_cmp = INVALID;
    unsigned pages_seen = 0;

    if (!vm_boot) {
        return PADDR_TO_KVADDR(ram_stealmem(npages));
    }
    
    spinlock_acquire(&cmp_spinlock);

    for (int i = 0; i < ram_pages; i++) {
        if (pages_seen == npages) {
            break;        
        } else if (cmp[i].free) {
            if (pages_seen == 0) {
                free_cmp = i;
            }
            pages_seen++;         
        } else {
            free_cmp = INVALID;
            pages_seen = 0;
        }
    }

    if (free_cmp == INVALID || pages_seen != npages) {
        return 0;
    }

    for (unsigned i = 0; i < npages; i++) {
        cmp[free_cmp + i].free = false;
        cmp[free_cmp + i].num_pages = npages;
    }
    vaddr_t alloc_addr = cmp[free_cmp].kvaddr;
    
    spinlock_release(&cmp_spinlock);

    return alloc_addr;
}


void free_kpages(vaddr_t addr) {
    int cmp_entry;
    unsigned npages;

    if (addr < cmp_end) {
        return;
    }
    spinlock_acquire(&cmp_spinlock);

    cmp_entry = (addr - cmp_end) / PAGE_SIZE;
    if ((addr - cmp_end) % PAGE_SIZE != 0) {
        cmp_entry++;
    }
    
    npages = cmp[cmp_entry].num_pages;
    for (unsigned i = 0; i < npages; i++) {
        cmp[cmp_entry + i].free = true;
        cmp[cmp_entry + i].num_pages = 0;
    }
    spinlock_release(&cmp_spinlock);
}


void vm_tlbshootdown_all(void) {

}


void vm_tlbshootdown(const struct tlbshootdown* shootdown) {
    (void) shootdown;
}