/*
 * Copyright (C) 2024 pdnguyen of the HCMC University of Technology
 */
/*
 * Source Code License Grant: Authors hereby grants to Licensee 
 * a personal to use and modify the Licensed Source Code for 
 * the sole purpose of studying during attending the course CO2018.
 */
//#ifdef MM_TLB
/*
 * Memory physical based TLB Cache
 * TLB cache module tlb/tlbcache.c
 *
 * TLB cache is physically memory phy
 * supports random access 
 * and runs at high speed
 */


#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

#define init_tlbcache(mp,sz,...) init_memphy(mp, sz, (1, ##__VA_ARGS__))

unsigned int lru_off_mask = 0b1011111;

/*
 *  tlb_cache_read read TLB cache device
 *  @mp: memphy struct
 *  @pid: process id
 *  @pgnum: page number
 *  @value: obtained value
 */

void printBits(unsigned int v) {
   unsigned int cv = v;
   char bit[33];
   unsigned int mask = (1<<31);
   for(int i=0; i<32; i++) {
      if( (mask & v) != 0 ) bit[i] = '1';
      else bit[i] = '0' ;
      mask = (mask >> 1);
   }
   bit[32] = '\0';
   printf("(%u) %s\n",cv ,bit );
}

int tlb_cache_read(struct memphy_struct * mp, int pid, int pgnum, BYTE value)
{
   /* TODO: the identify info is mapped to 
    *      cache line by employing:
    *      direct mapped, associated mapping etc.
    */

   /*    This will return the frame number ( in mem_ram )
    *    TLB cache will two way use associative maping, each data block will have 1 pte (32 bit)
    *    One entry of TLB Cache have: ( Valid(1bit), LRU_bit(1bit) Tag(30bit), Data(pte-32bit)) x 2
    *    Tag is the combine of pid(16bit) and page number(14bit, minus last bit for block offset), which help us define the pte
    *    
    * 
    *    The tag here is not optimized in number of bit, according to computer architecture couse,
    * we should eliminate the bits use for indexing (identifying set number) in cache, but for simpplification 
    * and various size of tlb_size in input file, we will use it.
    *    In total, we have 2*( 1 + 1 + 30 + 32) = 128 bit = 16B each TLB cache entry      
    */ 

   /*    This is the way we map (pid, page num) to entry in TLB cache: 
    *    We can concat [pid, pagenum] to 30bit number and modulo with tlb_size
    *    to know the set to put data into it.
    *    Since tlb_size usually small so we can see large bit ( pid ) in above concat wont affect much
    *    We can try for another maping method, like different way to concat, or hashing,...
    */

   const unsigned int num_tlb_entries = mp->maxsz / ENTRY_SIZE;

   unsigned int concat_address = (pid<<14) + pgnum; // retrive the combined address
   unsigned int tlbnum = ((pid<<5) + pgnum%(1<<5)) % num_tlb_entries;

   unsigned int phy_adr = tlbnum * ENTRY_SIZE;
   unsigned int tag, wvalue[2];

   unsigned int valid_mask = (1<<31);

   wvalue[0] = TLBMEMPHY_read_word(mp, phy_adr);
   wvalue[1] = TLBMEMPHY_read_word(mp, phy_adr+8);

   if(wvalue[0]==-1 || wvalue[1]==-1) return -1;
   
   for(int i=0; i<2; i++, phy_adr+=8) {
      if((wvalue[i] & valid_mask) ==0) continue;

      tag = wvalue[i] % (1<<30);
      if(tag == concat_address) {
         uint32_t pte = TLBMEMPHY_read_word(mp, phy_adr+4);  
         if(PAGING_PAGE_PRESENT(pte)) {
            TLBMEMPHY_write(mp, phy_adr, ((wvalue[i] >> 24) | 64)); // Set LRU of this block to 1
            TLBMEMPHY_write(mp, phy_adr + (i==0?8:-8), ((wvalue[1-i] >> 24) & lru_off_mask )); // Set LRU of other block to 0 
            return PAGING_FPN(pte);
         }
      }
   }

   return -1;
}

/*
 *  tlb_cache_write write TLB cache device
 *  @mp: memphy struct
 *  @pid: process id
 *  @pgnum: page number
 *  @value: obtained value
 */
int tlb_cache_write(struct memphy_struct *mp, struct pcb_t* proc, int pgnum)
{
   /* TODO: the identify info is mapped to 
    *      cache line by employing:
    *      direct mapped, associated mapping etc.
    */
   uint32_t pid = proc->pid;
   
   const unsigned int num_tlb_entries = mp->maxsz / ENTRY_SIZE;
   unsigned int concat_address = (pid<<14) + pgnum; // retrive the combined address
   unsigned int tlbnum = ((pid<<5) + pgnum%(1<<5)) % num_tlb_entries;

   unsigned int phy_adr = tlbnum * ENTRY_SIZE;
   unsigned int tag=concat_address, wvalue;


   for(int i=0; i<2; i++, phy_adr+=8) {
      BYTE value;
      TLBMEMPHY_read(mp, phy_adr, &value);
      if((value & (1<<7)) == 0) { // If valid = 0
         
         wvalue = ((3<<30) | tag);
         TLBMEMPHY_write_word(mp, phy_adr, wvalue);

         //TLBMEMPHY_dump(mp);

         if(i==0) {
            TLBMEMPHY_read(mp, phy_adr + 8, &value);
            TLBMEMPHY_write(mp, phy_adr + 8, (value & lru_off_mask)); // Set LRU other block to 0
         }
         else {
            TLBMEMPHY_read(mp, phy_adr - 8, &value);
            TLBMEMPHY_write(mp, phy_adr - 8, (value & lru_off_mask)); 
         }

         unsigned int pte = proc->mm->pgd[pgnum];
         TLBMEMPHY_write_word(mp,phy_adr+4,pte);

         return PAGING_FPN(pte);
      }
   }

   // If no valid = 0

   for(int i=0; i<2; i++, phy_adr+=8) {
      BYTE value;
      TLBMEMPHY_read(mp, phy_adr, &value);
      if((value & (1<<6)) == 0) { // If LRU = 0 _ Must have
         wvalue = ((3<<30) | concat_address);
         TLBMEMPHY_write_word(mp, phy_adr, wvalue);
         if(i==0) {
            TLBMEMPHY_read(mp, phy_adr + 8, &value);
            TLBMEMPHY_write(mp, phy_adr + 8, (value & lru_off_mask)); // Set LRU other block to 0
         }
         else {
            TLBMEMPHY_read(mp, phy_adr - 8, &value);
            TLBMEMPHY_write(mp, phy_adr - 8, (value & lru_off_mask)); 
         }

         unsigned int pte = proc->mm->pgd[pgnum];
         TLBMEMPHY_write_word(mp,phy_adr+4,pte);

         return PAGING_FPN(pte);
      }
   }

   return 0;
}

int tlb_cache_set_invalid(struct memphy_struct *mp, struct pcb_t* proc, int pgnum) {
   int pid = proc->pid;
   const unsigned int num_tlb_entries = mp->maxsz / ENTRY_SIZE;

   unsigned int concat_address = (pid<<14) + pgnum; // retrive the combined address
   unsigned int tlbnum = ((pid<<5) + pgnum%(1<<5)) % num_tlb_entries;

   unsigned int phy_adr = tlbnum * ENTRY_SIZE;
   unsigned int tag, wvalue[2];

   unsigned int valid_mask = (1<<31);

   wvalue[0] = TLBMEMPHY_read_word(mp, phy_adr);
   wvalue[1] = TLBMEMPHY_read_word(mp, phy_adr+8);

   if(wvalue[0]==-1 || wvalue[1]==-1) return -1;
   
   for(int i=0; i<2; i++, phy_adr+=8) {
      if((wvalue[i] & valid_mask) ==0) continue;

      tag = wvalue[i] % (1<<30);
      if(tag == concat_address) {
         TLBMEMPHY_write_word(mp, phy_adr, 0);
         TLBMEMPHY_write_word(mp, phy_adr+4, 0);
         return 0;
      }
   }
   return 0;
}

unsigned int TLBMEMPHY_read_word(struct memphy_struct * mp, int addr) {
   unsigned int val = 0;
   for(int i=0; i<4; i++) {
      BYTE value;
      if( TLBMEMPHY_read(mp, addr+i, &value) == -1) {
         perror("Cache reading error!");
         return -1;
      }
      val = (val<<8) + (int)value;
   }
   return val;
}

/*
 *  TLBMEMPHY_read natively supports MEMPHY device interfaces
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int TLBMEMPHY_read( struct memphy_struct * mp, int addr, BYTE *value)
{
   if (mp == NULL)
     return -1;

   pthread_mutex_lock(&mmvm_lock);
   /* TLB cached is random access by native */
   *value = mp->storage[addr];

   pthread_mutex_unlock(&mmvm_lock);

   return 0;
}

unsigned int TLBMEMPHY_write_word(struct memphy_struct * mp, int addr, unsigned int data) {
   //printf("DAT %u", data);
   unsigned int val = 0;
   unsigned int mask = 0b11111111;
   for(int i=3; i>=0; i--) {
      unsigned char value = (unsigned char)((data & mask)%256);
      //printf("BIT TEST %c, %u, %u - ", value, data, mask);
      //printBits((unsigned int)value);
      if( TLBMEMPHY_write(mp, addr+i, value) == -1) {
         perror("Cache writing error!");
         return -1;
      }
      data = (data >> 8);
   }
   return val;
}


/*
 *  TLBMEMPHY_write natively supports MEMPHY device interfaces
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int TLBMEMPHY_write(struct memphy_struct * mp, int addr, BYTE data)
{
   if (mp == NULL)
     return -1;

   pthread_mutex_lock(&mmvm_lock);
   /* TLB cached is random access by native */
   mp->storage[addr] = data;

   pthread_mutex_unlock(&mmvm_lock);

   return 0;
}

/*
 *  TLBMEMPHY_format natively supports MEMPHY device interfaces
 *  @mp: memphy struct
 */


int TLBMEMPHY_dump(struct memphy_struct * mp)
{
   /*TODO dump memphy contnt mp->storage 
    *     for tracing the memory content
    */
#ifdef OUTPUT_FOLDER
   FILE *output_file = mp->file;
   fprintf(output_file, "===== PHYSICAL MEMORY DUMP (TLB CACHE) =====\n");
#endif

   printf("\t\tPHYSICAL MEMORY (TLB CACHE) DUMP :\n");
   for (int i = 0; i < mp->maxsz; ++i)
   {
      if (mp->storage[i] != 0)
      {
#ifdef OUTPUT_FOLDER
         fprintf(output_file, "BYTE %08x: %d\n", i, mp->storage[i]);
#endif
         printf("BYTE %08x: %d\n", i, mp->storage[i]);
      }
   }
#ifdef OUTPUT_FOLDER
   fprintf(output_file, "===== PHYSICAL MEMORY END-DUMP (TLB CACHE)=====\n");
   fprintf(output_file, "================================================================\n");
#endif

   printf("\t\tPHYSICAL MEMORY END-DUMP\n");
   return 0;
}

int TLBMEMPHY_bin_dump(struct memphy_struct * mp)
{
   /*TODO dump memphy contnt mp->storage 
    *     for tracing the memory content
    */

   printf("\t*** PHYSICAL MEMORY (TLB CACHE) BIN DUMP:\n");
   for (int i = 0; i < mp->maxsz; i+=4)
   {
      if (mp->storage[i] != 0)
      {
#ifdef OUTPUT_FOLDER
         fprintf(output_file, "BYTE %08x: %d\n", i, mp->storage[i]);
#endif
         printf("\t   (%d) %08x: ",i, i);
         printBits(TLBMEMPHY_read_word(mp, i));
      }
   }

   printf("\t*** PHYSICAL MEMORY END-DUMP\n");
   return 0;
}


/*
 *  Init TLBMEMPHY struct
 */
int init_tlbmemphy(struct memphy_struct *mp, int max_size)
{
   mp->storage = (BYTE *)malloc(max_size*sizeof(BYTE));
   mp->maxsz = max_size;

   mp->rdmflg = 1;

   return 0;
}

//#endif
