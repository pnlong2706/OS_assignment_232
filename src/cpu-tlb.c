/*
 * Copyright (C) 2024 pdnguyen of the HCMC University of Technology
 */
/*
 * Source Code License Grant: Authors hereby grants to Licensee 
 * a personal to use and modify the Licensed Source Code for 
 * the sole purpose of studying during attending the course CO2018.
 */
//#ifdef CPU_TLB
/*
 * CPU TLB
 * TLB module cpu/cpu-tlb.c
 */
 
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

int tlb_change_all_page_tables_of(struct pcb_t *proc,  struct memphy_struct * mp)
{
  /* TODO update all page table directory info 
   *      in flush or wipe TLB (if needed)
   */

  return 0;
}

int tlb_flush_tlb_of(struct pcb_t *proc, struct memphy_struct * mp)
{
  /* TODO flush tlb cached*/

  return 0;
}

/*tlballoc - CPU TLB-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size 
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int tlballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
  int addr, val;
  /* By default using vmaid = 0 */
  
  val = __alloc(proc, 0, reg_index, size, &addr);
  if(val == -1 ) {
    return -1;
  }
  int pgn = addr / PAGING_PAGESZ;

  while( pgn * 256 < addr + size ) {
    tlb_cache_write(proc->tlb, proc, pgn);
    pgn++;
  }
  /* TODO update TLB CACHED frame num of the new allocated page(s)*/
  /* by using tlb_cache_read()/tlb_cache_write()*/

#ifdef IODUMP
  printf("\t*** Alocation: PID=%d - Region=%d - Address=%08x - Size=%d byte\n", proc->pid, reg_index, addr, size);
  TLBMEMPHY_bin_dump(proc->tlb);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}

/*pgfree - CPU TLB-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size 
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int tlbfree_data(struct pcb_t *proc, uint32_t reg_index)
{

  struct vm_rg_struct *rgnode = get_symrg_byid(proc->mm, reg_index);
  int pg_st = rgnode->rg_start / PAGING_PAGESZ;
  int pg_ed = (rgnode->rg_end-1) / PAGING_PAGESZ;
  
  __free(proc, 0, reg_index);

  /* TODO update TLB CACHED frame num of freed page(s)*/
  /* by using tlb_cache_read()/tlb_cache_write()*/
  unsigned int num_tlb_entries = proc->tlb->maxsz / ENTRY_SIZE;

  for(int pgn = pg_st; pgn <= pg_ed; pgn++) {

    unsigned int concat_address = (proc->pid<<14) + pgn; // retrive the combined address
    unsigned int tlbnum = concat_address % num_tlb_entries;

    unsigned int phy_adr = tlbnum * ENTRY_SIZE;

    BYTE temp;
    TLBMEMPHY_read(proc->tlb, phy_adr, &temp);
    TLBMEMPHY_write(proc->tlb, phy_adr, (temp & ((1<<7)-1))); // valid bit to 0
  }

  return 0;
}


/*tlbread - CPU TLB-based read a region memory
 *@proc: Process executing the instruction
 *@source: index of source register
 *@offset: source address = [source] + [offset]
 *@destination: destination storage
 */
int tlbread(struct pcb_t * proc, uint32_t source,
            uint32_t offset, 	uint32_t destination) 
{
  BYTE data;
  int frmnum = -1, val;
	
  /* TODO retrieve TLB CACHED frame num of accessing page(s)*/
  /* by using tlb_cache_read()/tlb_cache_write()*/
  /* frmnum is return value of tlb_cache_read/write value*/

  struct vm_rg_struct *currg = get_symrg_byid(proc->mm, source);
  int addr = currg->rg_start + offset;
  int pgn = PAGING_PGN(addr);

  val = __read(proc, 0, source, offset, &data);
  if(val == -1) return -1;
  frmnum = tlb_cache_read(proc->tlb, proc->pid, pgn, data);

#ifdef IODUMP
  if (frmnum >= 0) {
    printf("\tTLB hit at read region=%d offset=%d\n", 
	         source, offset);
    pthread_mutex_lock(&mmvm_lock);
    proc->stat_hit_time ++;
    pthread_mutex_unlock(&mmvm_lock);
  }
  else {
    printf("\tTLB miss at read region=%d offset=%d\n", 
	         source, offset);
    pthread_mutex_lock(&mmvm_lock);
    proc->stat_miss_time ++;
    pthread_mutex_unlock(&mmvm_lock);
    tlb_cache_write(proc->tlb, proc, pgn);
  }
  
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  MEMPHY_dump(proc->mram);
#endif

  destination = (uint32_t) data;

  /* TODO update TLB CACHED with frame num of recent accessing page(s)*/
  /* by using tlb_cache_read()/tlb_cache_write()*/

  return val;
}

/*tlbwrite - CPU TLB-based write a region memory
 *@proc: Process executing the instruction
 *@data: data to be wrttien into memory
 *@destination: index of destination register
 *@offset: destination address = [destination] + [offset]
 */

int tlbwrite(struct pcb_t * proc, BYTE data,
             uint32_t destination, uint32_t offset)
{
  int frmnum = -1, val;

  /* TODO retrieve TLB CACHED frame num of accessing page(s))*/
  /* by using tlb_cache_read()/tlb_cache_write()
  frmnum is return value of tlb_cache_read/write value*/

  struct vm_rg_struct *currg = get_symrg_byid(proc->mm, destination);
  int addr = currg->rg_start + offset;
  int pgn = PAGING_PGN(addr);

  frmnum = tlb_cache_read(proc->tlb, proc->pid, pgn, data);
  val = __write(proc, 0, destination, offset, data);

  if(val==-1) return -1;

#ifdef IODUMP
  if (frmnum >= 0) {
    printf("\tTLB hit at write region=%d offset=%d value=%d\n",
	          destination, offset, data);
    pthread_mutex_lock(&mmvm_lock);
    proc->stat_hit_time ++;
    pthread_mutex_unlock(&mmvm_lock);
  }
	else {
    printf("\tTLB miss at write region=%d offset=%d value=%d\n",
            destination, offset, data);
    pthread_mutex_lock(&mmvm_lock);
    proc->stat_miss_time ++;
    pthread_mutex_unlock(&mmvm_lock);
    tlb_cache_write(proc->tlb, proc, pgn);
  }

#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  MEMPHY_dump(proc->mram);
#endif
  
  /* TODO update TLB CACHED with frame num of recent accessing page(s)*/
  /* by using tlb_cache_read()/tlb_cache_write()*/

  return val;
}

//#endif
