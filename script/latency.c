#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#define CHA_BASE 0x0E00L
#define NUM_CHA 26
#define NUM_CTR 2
#define CHA_OFFSET 0x10
#define OCC_IDX 0
#define INS_IDX 1

int msr_fd;
uint64_t cur_ctr_tsc[NUM_CHA][NUM_CTR], prev_ctr_tsc[NUM_CHA][NUM_CTR], ctr_tsc[NUM_CHA][NUM_CTR];
uint64_t cur_ctr_val[NUM_CHA][NUM_CTR], prev_ctr_val[NUM_CHA][NUM_CTR], ctr_val[NUM_CHA][NUM_CTR];

uint64_t rdtscp(void) {
  uint32_t eax, edx;  
  __asm volatile ("rdtscp" : "=a" (eax), "=d" (edx) :: "ecx", "memory"); 
  return ((uint64_t)edx << 32) | eax; 
}

void update_stats() {
  int cha, ctr;	
  for(cha = 0; cha < NUM_CHA; cha++) {
        for(ctr = 0; ctr < NUM_CTR; ctr++) {
	    ctr_tsc[cha][ctr] = cur_ctr_tsc[cha][ctr] - prev_ctr_tsc[cha][ctr];
	    // if(ctr == OCC_IDX) 
            //     ctr_val[cha][ctr] = (cur_ctr_val[cha][ctr] - prev_ctr_val[cha][ctr])/ctr_tsc[cha][ctr];
	    // else 
	    ctr_val[cha][ctr] = (cur_ctr_val[cha][ctr] - prev_ctr_val[cha][ctr]);
	}
  }
}

void sample_cha_ctr(int cha, int ctr) {
    uint32_t msr_num;
    uint64_t msr_val;
    int ret;

    msr_num = CHA_BASE + (CHA_OFFSET * cha) + ctr + 8;
    ret = pread(msr_fd, &msr_val, sizeof(msr_val), msr_num);
    if (ret != sizeof(msr_val)) {
        perror("ERROR: failed to read MSR");
    }
    // printf("cha ctr msr_val : %d %d %ld\n", cha, ctr, msr_val);
    prev_ctr_val[cha][ctr] = cur_ctr_val[cha][ctr];
    cur_ctr_val[cha][ctr] = msr_val;
    prev_ctr_tsc[cha][ctr] = cur_ctr_tsc[cha][ctr];
    cur_ctr_tsc[cha][ctr] = rdtscp();
}

void sample_cha_all() {
  int cha, ctr;
  for(cha = 0; cha < NUM_CHA; cha++) {
        for(ctr = 0; ctr < NUM_CTR; ctr++) {
            sample_cha_ctr(cha, ctr);
	}
  }
  update_stats();
}

void cha_setup(int cpu) {
  char filename[100];
  sprintf(filename, "/dev/cpu/%d/msr", cpu);
  msr_fd = open(filename, O_RDWR);
  if(msr_fd == -1) {
    perror("Failed to open msr file");
  }

  // Setup counters
  int cha, ctr, ret;
  uint32_t msr_num;
  uint64_t msr_val;
  for(cha = 0; cha < NUM_CHA; cha++) {
      // Filter0
      msr_num = CHA_BASE + (CHA_OFFSET * cha) + 5; 
      msr_val = 0x00000000; 
      ret = pwrite(msr_fd,&msr_val,sizeof(msr_val),msr_num);
      if (ret != 8) {
	  printf("wrmsr FILTER0 failed for cha: %d\n", cha);
          perror("wrmsr FILTER0 failed");
      }

      // Filter1
      msr_num = CHA_BASE + (CHA_OFFSET * cha) + 6; 
      msr_val = (cha%2 == 0)?(0x12d40432):(0x12d40431);
      ret = pwrite(msr_fd,&msr_val,sizeof(msr_val),msr_num);
      if (ret != 8) {
          perror("wrmsr FILTER1 failed");
      }
      
      // Counter0
      msr_num = CHA_BASE + (CHA_OFFSET * cha) + 1; 
      msr_val = 0x00402136; // TOR Occupancy
      ret = pwrite(msr_fd,&msr_val,sizeof(msr_val),msr_num);
      if (ret != 8) {
          perror("wrmsr COUNTER0 failed");
      }

      // Counter1
      msr_num = CHA_BASE + (CHA_OFFSET * cha) + 2; 
      msr_val = 0x00402135; // TOR Inserts
      ret = pwrite(msr_fd,&msr_val,sizeof(msr_val),msr_num);
      if (ret != 8) {
          perror("wrmsr COUNTER1 failed");
      }

      // Counter2
      msr_num = CHA_BASE + (CHA_OFFSET * cha) + 3; 
      msr_val = 0x400000; // CLOCKTICKS
      ret = pwrite(msr_fd,&msr_val,sizeof(msr_val),msr_num);
      if (ret != 8) {
          perror("wrmsr COUNTER2 failed");
      }
      
      // Counter3
      msr_num = CHA_BASE + (CHA_OFFSET * cha) + 4; 
      msr_val = 0x400000; // CLOCKTICKS
      ret = pwrite(msr_fd,&msr_val,sizeof(msr_val),msr_num);
      if (ret != 8) {
          perror("wrmsr COUNTER3 failed");
      }
  }

  // Initialize stats
  for(cha = 0; cha < NUM_CHA; cha++) {
        for(ctr = 0; ctr < NUM_CTR; ctr++) {
            cur_ctr_tsc[cha][ctr] = 0;
            cur_ctr_val[cha][ctr] = 0;
            ctr_val[cha][ctr] = 0;
        }
    }
    sample_cha_all();
}

void print_cha(int cha) {
  // printf("%s CHA%d OCC : %ld\n", (cha%2 == 0)?("LOC"):("REM"), cha, ctr_val[cha][OCC_IDX]);
  // printf("%s CHA%d INS : %ld\n", (cha%2 == 0)?("LOC"):("REM"), cha, ctr_val[cha][INS_IDX]);
  printf("%s CHA%d LAT : %ld\n", (cha%2 == 0)?("LOC"):("REM"), cha, ctr_val[cha][OCC_IDX]/ctr_val[cha][INS_IDX]);
}

void print_cha_all() {
  uint64_t rem_occ = 0;
  uint64_t rem_ins = 0;
  uint64_t loc_occ = 0;
  uint64_t loc_ins = 0;
  uint64_t tot_occ = 0;
  uint64_t tot_ins = 0;
  int cha, ctr;
  for(cha = 0; cha < NUM_CHA; cha++) {
	if(cha % 2 == 0) {
	    loc_ins += ctr_val[cha][INS_IDX];
    	    loc_occ += ctr_val[cha][OCC_IDX];
	}
	else {
	    rem_ins += ctr_val[cha][INS_IDX];
	    rem_occ += ctr_val[cha][OCC_IDX];
    	}
	print_cha(cha);
  }
  printf("LOC LAT : %ld\n", loc_occ/loc_ins); 
  printf("REM LAT : %ld\n", rem_occ/rem_ins);
  printf("TOT LAT : %ld\n", (loc_occ+rem_occ)/(loc_ins+rem_ins));
}

void main() {
  cha_setup(30);
  while(1) {
      sleep(1);
      sample_cha_all();
      print_cha_all();
  }
}
