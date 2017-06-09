/*
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/include/linux/minix_fs.h
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#ifndef CSC369_EXT2_FS_H
#define CSC369_EXT2_FS_H
//
// ext2all.c
//
// List entries in the all directories of the floppy disk.
// Assumes no directory is more than one block in size.
//
// Nick Howe
// Emanuele Altieri
//
///////////////////////////////////////////////////////////////////////////////
//
// HEADER FILES
//

#define __LINUX_PERCPU_H
// avoid including files that generate errors

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ext2.h"

///////////////////////////////////////////////////////////////////////////////
//
//  DEFINITIONS AND MACROS
//

#define BASE_OFFSET 1024  // beginning of the super block (first group) 
#define USB_DEVICE "/dev/sda1"     // the memory stick device 

#define BLOCK_OFFSET(block) (BASE_OFFSET+(block-1)*block_size)


///////////////////////////////////////////////////////////////////////////////
//
//  GLOBALS
//

static unsigned int block_size = 0;        // to be calculated
static unsigned int ptrs_per_block = 0;    // to be calculated
static unsigned int num_inodes_per_group = 0;  // to be read in
static unsigned int num_inodes = 0;        // to be read in

///////////////////////////////////////////////////////////////////////////////
//
//  FORWARD DECLARATIONS
//

static void read_block(int, int, void*);
static void read_inode(int, const struct ext2_group_desc*, int,
		       struct ext2_inode*);
static void *read_file(int , const struct ext2_group_desc*, 
		       const struct ext2_inode*);
static void print_dir(int, const struct ext2_group_desc*,
		      const struct ext2_inode*);

///////////////////////////////////////////////////////////////////////////////
//
//  main() opens the device and initializes some of the data structures.
//

int main(void) {
  struct ext2_super_block super;
  struct ext2_group_desc *group;
  struct ext2_inode inode;
  int i;
  int fd;
  int num_groups;

  // open usb device 
  if ((fd = open(USB_DEVICE, O_RDONLY)) < 0) {
    perror(USB_DEVICE);
    exit(1);  // error while opening the floppy device 
  }

  // read super-block 
  lseek(fd, BASE_OFFSET, SEEK_SET); 
  read(fd, &super, sizeof(super));

  if (super.s_magic != EXT2_SUPER_MAGIC) {
    fprintf(stderr, "Not an Ext2 filesystem\n");
    exit(1);
  }
		
  block_size = 1024 << super.s_log_block_size;
  ptrs_per_block = (block_size/sizeof(__u32));
  num_inodes_per_group = super.s_inodes_per_group;

  // create and read in group descriptors array
  num_groups = (super.s_blocks_count+super.s_blocks_per_group-1)
    /super.s_blocks_per_group;
  if ((group = (struct ext2_group_desc*)
       malloc(num_groups*sizeof(struct ext2_group_desc))) == NULL) {
    fprintf(stderr, "Memory error\n");
    close(fd);
    exit(1);
  }
  for (i = 0; i < num_groups; i++) {
    lseek(fd,BASE_OFFSET+block_size+i*sizeof(struct ext2_group_desc),SEEK_SET);
    read(fd, group+i, sizeof(struct ext2_group_desc));
  }

  // show entries in the root directory
  read_inode(fd, group, 2, &inode);   // read inode 2 (root directory) 
  printf("   inode    listing\n---------- ----------\n");
  print_dir(fd, group, &inode);

  free(group);
  close(fd);
  exit(0);
} // end of main() 

///////////////////////////////////////////////////////////////////////////////
//
//  read_block() reads the specified block of data
//

static 
void read_block(int fd, int block_no, void *buffer)
{
  lseek(fd, BLOCK_OFFSET(block_no),SEEK_SET);
  read(fd, buffer, block_size);
} // end of read_block()

///////////////////////////////////////////////////////////////////////////////
//
//  read_inode() reads in an inode data structure
//
//  R/O:  fd, group,inode_no
//  W/O:  inode

static 
void read_inode(int fd, const struct ext2_group_desc *group, int inode_no, 
		struct ext2_inode *inode)
{
  int group_no = inode_no/num_inodes_per_group;
  //printf("group #%d; table %d\n",group_no,group[group_no].bg_inode_table);
  lseek(fd, BLOCK_OFFSET(group[group_no].bg_inode_table)+(inode_no-group_no*num_inodes_per_group-1)*sizeof(struct ext2_inode),SEEK_SET);
  read(fd, inode, sizeof(struct ext2_inode));
} // end of read_inode()

///////////////////////////////////////////////////////////////////////////////
//
//  read_file() reads in a file, allocating buffer space as necessary
//  (Note that this may not be feasible if the file is too big to fit in
//  main memory.)
//
//  R/O:  fd, group, inode

static 
void *read_file(int fd, const struct ext2_group_desc *group, 
		const struct ext2_inode *inode)
{
  void *buffer;
  __u32 *si_block, *di_block, *ti_block;
  int num_read = 0;
  int si_count, di_count, ti_count;

  // allocate space for file
  if ((buffer = malloc(block_size*inode->i_blocks)) == NULL) {
      fprintf(stderr, "Memory error\n");
      close(fd);
      exit(1);
  }

  // read direct blocks
  while ((num_read < inode->i_blocks)&&(num_read < EXT2_NDIR_BLOCKS)) {
    lseek(fd, BLOCK_OFFSET(inode->i_block[num_read]),SEEK_SET);
    read(fd, buffer+num_read*block_size, block_size);
    num_read++;
  }

  // read indirect blocks, if necessary
  if (num_read < inode->i_blocks) {
    // allocate space for indirect block pointers
    if ((si_block = malloc(3*block_size)) == NULL) {
      fprintf(stderr, "Memory error\n");
      close(fd);
      exit(1);
    }
    di_block = si_block+ptrs_per_block;
    ti_block = di_block+ptrs_per_block;
    
    // single indirect
    lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_IND_BLOCK]),SEEK_SET);
    read(fd, si_block, block_size);
    si_count = 0;
    while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
      lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
      read(fd, buffer+num_read*block_size, block_size);
      num_read++;
      si_count++;
    }

    // double indirect
    if (num_read < inode->i_blocks) {
      lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]),SEEK_SET);
      read(fd, di_block, block_size);
      di_count = 0;
      while ((num_read < inode->i_blocks)&&(di_count < ptrs_per_block)) {
	lseek(fd, BLOCK_OFFSET(di_block[di_count]),SEEK_SET);
	read(fd, si_block, block_size);
	si_count = 0;
	while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
	  lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
	  read(fd, buffer+num_read*block_size, block_size);
	  num_read++;
	  si_count++;
	}
	di_count++; 
      }
    }

    // triple indirect
    if (num_read < inode->i_blocks) {
      lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_TIND_BLOCK]),SEEK_SET);
      read(fd, ti_block, block_size);
      ti_count = 0;
      while ((num_read < inode->i_blocks)&&(ti_count < ptrs_per_block)) {
	lseek(fd,BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]),SEEK_SET);
	read(fd, di_block, block_size);
	di_count = 0;
	while ((num_read < inode->i_blocks)&&(di_count < ptrs_per_block)) {
	  lseek(fd, BLOCK_OFFSET(di_block[di_count]),SEEK_SET);
	  read(fd, si_block, block_size);
	  si_count = 0;
	  while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
	    lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
	    read(fd, buffer+num_read*block_size, block_size);
	    num_read++;
	    si_count++;
	  }
	  di_count++;
	}
	ti_count++;
      }
    }
    
    free(si_block);
  }
  
  return buffer;
} // end of read_file()

///////////////////////////////////////////////////////////////////////////////
//
//  print_dir() reads in a directory structure
//

static void print_dir(int fd, const struct ext2_group_desc *group,
		     const struct ext2_inode *inode)
{
  void *buffer;

  if (S_ISDIR(inode->i_mode)) {
    struct ext2_dir_entry_2 *entry;
    unsigned int size = 0;

    // read in file (directory) data
    buffer = read_file(fd, group, inode);

    // print out directory
    entry = (struct ext2_dir_entry_2 *) buffer;  // first entry
    while(size < inode->i_size) {
      if (entry->inode > 0) {
	// print out information on this entry 
	char file_name[EXT2_NAME_LEN+1];
	memcpy(file_name, entry->name, entry->name_len);
	file_name[entry->name_len] = 0; // append null to the file name
	printf("%10u %s\n", entry->inode, file_name);
      }
    
      // move on to next entry in this directory 
      size += entry->rec_len;
      entry = (void*) entry + entry->rec_len;
    }
     
    free(buffer);
  }
} // print_dir() 

///////////////////////////////////////////////////////////////////////////////
//
// ext2all.c
//
// List entries in the all directories of the floppy disk.
// Assumes no directory is more than one block in size.
//
// Nick Howe
// Emanuele Altieri
//
///////////////////////////////////////////////////////////////////////////////
//
// HEADER FILES
//

#define __LINUX_PERCPU_H
// avoid including files that generate errors

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ext2.h"

///////////////////////////////////////////////////////////////////////////////
//
//  DEFINITIONS AND MACROS
//

#define BASE_OFFSET 1024  // beginning of the super block (first group) 
#define USB_DEVICE "/dev/sda1"     // the memory stick device 

#define BLOCK_OFFSET(block) (BASE_OFFSET+(block-1)*block_size)


///////////////////////////////////////////////////////////////////////////////
//
//  GLOBALS
//

static unsigned int block_size = 0;        // to be calculated
static unsigned int ptrs_per_block = 0;    // to be calculated
static unsigned int num_inodes_per_group = 0;  // to be read in
static unsigned int num_inodes = 0;        // to be read in

///////////////////////////////////////////////////////////////////////////////
//
//  FORWARD DECLARATIONS
//

static void read_block(int, int, void*);
static void read_inode(int, const struct ext2_group_desc*, int,
		       struct ext2_inode*);
static void *read_file(int , const struct ext2_group_desc*, 
		       const struct ext2_inode*);
static void print_dir(int, const struct ext2_group_desc*,
		      const struct ext2_inode*);

///////////////////////////////////////////////////////////////////////////////
//
//  main() opens the device and initializes some of the data structures.
//

int main(void) {
  struct ext2_super_block super;
  struct ext2_group_desc *group;
  struct ext2_inode inode;
  int i;
  int fd;
  int num_groups;

  // open usb device 
  if ((fd = open(USB_DEVICE, O_RDONLY)) < 0) {
    perror(USB_DEVICE);
    exit(1);  // error while opening the floppy device 
  }

  // read super-block 
  lseek(fd, BASE_OFFSET, SEEK_SET); 
  read(fd, &super, sizeof(super));

  if (super.s_magic != EXT2_SUPER_MAGIC) {
    fprintf(stderr, "Not an Ext2 filesystem\n");
    exit(1);
  }
		
  block_size = 1024 << super.s_log_block_size;
  ptrs_per_block = (block_size/sizeof(__u32));
  num_inodes_per_group = super.s_inodes_per_group;

  // create and read in group descriptors array
  num_groups = (super.s_blocks_count+super.s_blocks_per_group-1)
    /super.s_blocks_per_group;
  if ((group = (struct ext2_group_desc*)
       malloc(num_groups*sizeof(struct ext2_group_desc))) == NULL) {
    fprintf(stderr, "Memory error\n");
    close(fd);
    exit(1);
  }
  for (i = 0; i < num_groups; i++) {
    lseek(fd,BASE_OFFSET+block_size+i*sizeof(struct ext2_group_desc),SEEK_SET);
    read(fd, group+i, sizeof(struct ext2_group_desc));
  }

  // show entries in the root directory
  read_inode(fd, group, 2, &inode);   // read inode 2 (root directory) 
  printf("   inode    listing\n---------- ----------\n");
  print_dir(fd, group, &inode);

  free(group);
  close(fd);
  exit(0);
} // end of main() 

///////////////////////////////////////////////////////////////////////////////
//
//  read_block() reads the specified block of data
//

static 
void read_block(int fd, int block_no, void *buffer)
{
  lseek(fd, BLOCK_OFFSET(block_no),SEEK_SET);
  read(fd, buffer, block_size);
} // end of read_block()

///////////////////////////////////////////////////////////////////////////////
//
//  read_inode() reads in an inode data structure
//
//  R/O:  fd, group,inode_no
//  W/O:  inode

static 
void read_inode(int fd, const struct ext2_group_desc *group, int inode_no, 
		struct ext2_inode *inode)
{
  int group_no = inode_no/num_inodes_per_group;
  //printf("group #%d; table %d\n",group_no,group[group_no].bg_inode_table);
  lseek(fd, BLOCK_OFFSET(group[group_no].bg_inode_table)+(inode_no-group_no*num_inodes_per_group-1)*sizeof(struct ext2_inode),SEEK_SET);
  read(fd, inode, sizeof(struct ext2_inode));
} // end of read_inode()

///////////////////////////////////////////////////////////////////////////////
//
//  read_file() reads in a file, allocating buffer space as necessary
//  (Note that this may not be feasible if the file is too big to fit in
//  main memory.)
//
//  R/O:  fd, group, inode

static 
void *read_file(int fd, const struct ext2_group_desc *group, 
		const struct ext2_inode *inode)
{
  void *buffer;
  __u32 *si_block, *di_block, *ti_block;
  int num_read = 0;
  int si_count, di_count, ti_count;

  // allocate space for file
  if ((buffer = malloc(block_size*inode->i_blocks)) == NULL) {
      fprintf(stderr, "Memory error\n");
      close(fd);
      exit(1);
  }

  // read direct blocks
  while ((num_read < inode->i_blocks)&&(num_read < EXT2_NDIR_BLOCKS)) {
    lseek(fd, BLOCK_OFFSET(inode->i_block[num_read]),SEEK_SET);
    read(fd, buffer+num_read*block_size, block_size);
    num_read++;
  }

  // read indirect blocks, if necessary
  if (num_read < inode->i_blocks) {
    // allocate space for indirect block pointers
    if ((si_block = malloc(3*block_size)) == NULL) {
      fprintf(stderr, "Memory error\n");
      close(fd);
      exit(1);
    }
    di_block = si_block+ptrs_per_block;
    ti_block = di_block+ptrs_per_block;
    
    // single indirect
    lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_IND_BLOCK]),SEEK_SET);
    read(fd, si_block, block_size);
    si_count = 0;
    while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
      lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
      read(fd, buffer+num_read*block_size, block_size);
      num_read++;
      si_count++;
    }

    // double indirect
    if (num_read < inode->i_blocks) {
      lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]),SEEK_SET);
      read(fd, di_block, block_size);
      di_count = 0;
      while ((num_read < inode->i_blocks)&&(di_count < ptrs_per_block)) {
	lseek(fd, BLOCK_OFFSET(di_block[di_count]),SEEK_SET);
	read(fd, si_block, block_size);
	si_count = 0;
	while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
	  lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
	  read(fd, buffer+num_read*block_size, block_size);
	  num_read++;
	  si_count++;
	}
	di_count++; 
      }
    }

    // triple indirect
    if (num_read < inode->i_blocks) {
      lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_TIND_BLOCK]),SEEK_SET);
      read(fd, ti_block, block_size);
      ti_count = 0;
      while ((num_read < inode->i_blocks)&&(ti_count < ptrs_per_block)) {
	lseek(fd,BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]),SEEK_SET);
	read(fd, di_block, block_size);
	di_count = 0;
	while ((num_read < inode->i_blocks)&&(di_count < ptrs_per_block)) {
	  lseek(fd, BLOCK_OFFSET(di_block[di_count]),SEEK_SET);
	  read(fd, si_block, block_size);
	  si_count = 0;
	  while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
	    lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
	    read(fd, buffer+num_read*block_size, block_size);
	    num_read++;
	    si_count++;
	  }
	  di_count++;
	}
	ti_count++;
      }
    }
    
    free(si_block);
  }
  
  return buffer;
} // end of read_file()

///////////////////////////////////////////////////////////////////////////////
//
//  print_dir() reads in a directory structure
//

static void print_dir(int fd, const struct ext2_group_desc *group,
		     const struct ext2_inode *inode)
{
  void *buffer;

  if (S_ISDIR(inode->i_mode)) {
    struct ext2_dir_entry_2 *entry;
    unsigned int size = 0;

    // read in file (directory) data
    buffer = read_file(fd, group, inode);

    // print out directory
    entry = (struct ext2_dir_entry_2 *) buffer;  // first entry
    while(size < inode->i_size) {
      if (entry->inode > 0) {
	// print out information on this entry 
	char file_name[EXT2_NAME_LEN+1];
	memcpy(file_name, entry->name, entry->name_len);
	file_name[entry->name_len] = 0; // append null to the file name
	printf("%10u %s\n", entry->inode, file_name);
      }
    
      // move on to next entry in this directory 
      size += entry->rec_len;
      entry = (void*) entry + entry->rec_len;
    }
     
    free(buffer);
  }
} // print_dir() 

///////////////////////////////////////////////////////////////////////////////
//
// ext2all.c
//
// List entries in the all directories of the floppy disk.
// Assumes no directory is more than one block in size.
//
// Nick Howe
// Emanuele Altieri
//
///////////////////////////////////////////////////////////////////////////////
//
// HEADER FILES
//

#define __LINUX_PERCPU_H
// avoid including files that generate errors

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ext2.h"

///////////////////////////////////////////////////////////////////////////////
//
//  DEFINITIONS AND MACROS
//

#define BASE_OFFSET 1024  // beginning of the super block (first group) 
#define USB_DEVICE "/dev/sda1"     // the memory stick device 

#define BLOCK_OFFSET(block) (BASE_OFFSET+(block-1)*block_size)


///////////////////////////////////////////////////////////////////////////////
//
//  GLOBALS
//

static unsigned int block_size = 0;        // to be calculated
static unsigned int ptrs_per_block = 0;    // to be calculated
static unsigned int num_inodes_per_group = 0;  // to be read in
static unsigned int num_inodes = 0;        // to be read in

///////////////////////////////////////////////////////////////////////////////
//
//  FORWARD DECLARATIONS
//

static void read_block(int, int, void*);
static void read_inode(int, const struct ext2_group_desc*, int,
		       struct ext2_inode*);
static void *read_file(int , const struct ext2_group_desc*, 
		       const struct ext2_inode*);
static void print_dir(int, const struct ext2_group_desc*,
		      const struct ext2_inode*);

///////////////////////////////////////////////////////////////////////////////
//
//  main() opens the device and initializes some of the data structures.
//

int main(void) {
  struct ext2_super_block super;
  struct ext2_group_desc *group;
  struct ext2_inode inode;
  int i;
  int fd;
  int num_groups;

  // open usb device 
  if ((fd = open(USB_DEVICE, O_RDONLY)) < 0) {
    perror(USB_DEVICE);
    exit(1);  // error while opening the floppy device 
  }

  // read super-block 
  lseek(fd, BASE_OFFSET, SEEK_SET); 
  read(fd, &super, sizeof(super));

  if (super.s_magic != EXT2_SUPER_MAGIC) {
    fprintf(stderr, "Not an Ext2 filesystem\n");
    exit(1);
  }
		
  block_size = 1024 << super.s_log_block_size;
  ptrs_per_block = (block_size/sizeof(__u32));
  num_inodes_per_group = super.s_inodes_per_group;

  // create and read in group descriptors array
  num_groups = (super.s_blocks_count+super.s_blocks_per_group-1)
    /super.s_blocks_per_group;
  if ((group = (struct ext2_group_desc*)
       malloc(num_groups*sizeof(struct ext2_group_desc))) == NULL) {
    fprintf(stderr, "Memory error\n");
    close(fd);
    exit(1);
  }
  for (i = 0; i < num_groups; i++) {
    lseek(fd,BASE_OFFSET+block_size+i*sizeof(struct ext2_group_desc),SEEK_SET);
    read(fd, group+i, sizeof(struct ext2_group_desc));
  }

  // show entries in the root directory
  read_inode(fd, group, 2, &inode);   // read inode 2 (root directory) 
  printf("   inode    listing\n---------- ----------\n");
  print_dir(fd, group, &inode);

  free(group);
  close(fd);
  exit(0);
} // end of main() 

///////////////////////////////////////////////////////////////////////////////
//
//  read_block() reads the specified block of data
//

static 
void read_block(int fd, int block_no, void *buffer)
{
  lseek(fd, BLOCK_OFFSET(block_no),SEEK_SET);
  read(fd, buffer, block_size);
} // end of read_block()

///////////////////////////////////////////////////////////////////////////////
//
//  read_inode() reads in an inode data structure
//
//  R/O:  fd, group,inode_no
//  W/O:  inode

static 
void read_inode(int fd, const struct ext2_group_desc *group, int inode_no, 
		struct ext2_inode *inode)
{
  int group_no = inode_no/num_inodes_per_group;
  //printf("group #%d; table %d\n",group_no,group[group_no].bg_inode_table);
  lseek(fd, BLOCK_OFFSET(group[group_no].bg_inode_table)+(inode_no-group_no*num_inodes_per_group-1)*sizeof(struct ext2_inode),SEEK_SET);
  read(fd, inode, sizeof(struct ext2_inode));
} // end of read_inode()

///////////////////////////////////////////////////////////////////////////////
//
//  read_file() reads in a file, allocating buffer space as necessary
//  (Note that this may not be feasible if the file is too big to fit in
//  main memory.)
//
//  R/O:  fd, group, inode

static 
void *read_file(int fd, const struct ext2_group_desc *group, 
		const struct ext2_inode *inode)
{
  void *buffer;
  __u32 *si_block, *di_block, *ti_block;
  int num_read = 0;
  int si_count, di_count, ti_count;

  // allocate space for file
  if ((buffer = malloc(block_size*inode->i_blocks)) == NULL) {
      fprintf(stderr, "Memory error\n");
      close(fd);
      exit(1);
  }

  // read direct blocks
  while ((num_read < inode->i_blocks)&&(num_read < EXT2_NDIR_BLOCKS)) {
    lseek(fd, BLOCK_OFFSET(inode->i_block[num_read]),SEEK_SET);
    read(fd, buffer+num_read*block_size, block_size);
    num_read++;
  }

  // read indirect blocks, if necessary
  if (num_read < inode->i_blocks) {
    // allocate space for indirect block pointers
    if ((si_block = malloc(3*block_size)) == NULL) {
      fprintf(stderr, "Memory error\n");
      close(fd);
      exit(1);
    }
    di_block = si_block+ptrs_per_block;
    ti_block = di_block+ptrs_per_block;
    
    // single indirect
    lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_IND_BLOCK]),SEEK_SET);
    read(fd, si_block, block_size);
    si_count = 0;
    while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
      lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
      read(fd, buffer+num_read*block_size, block_size);
      num_read++;
      si_count++;
    }

    // double indirect
    if (num_read < inode->i_blocks) {
      lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]),SEEK_SET);
      read(fd, di_block, block_size);
      di_count = 0;
      while ((num_read < inode->i_blocks)&&(di_count < ptrs_per_block)) {
	lseek(fd, BLOCK_OFFSET(di_block[di_count]),SEEK_SET);
	read(fd, si_block, block_size);
	si_count = 0;
	while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
	  lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
	  read(fd, buffer+num_read*block_size, block_size);
	  num_read++;
	  si_count++;
	}
	di_count++; 
      }
    }

    // triple indirect
    if (num_read < inode->i_blocks) {
      lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_TIND_BLOCK]),SEEK_SET);
      read(fd, ti_block, block_size);
      ti_count = 0;
      while ((num_read < inode->i_blocks)&&(ti_count < ptrs_per_block)) {
	lseek(fd,BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]),SEEK_SET);
	read(fd, di_block, block_size);
	di_count = 0;
	while ((num_read < inode->i_blocks)&&(di_count < ptrs_per_block)) {
	  lseek(fd, BLOCK_OFFSET(di_block[di_count]),SEEK_SET);
	  read(fd, si_block, block_size);
	  si_count = 0;
	  while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
	    lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
	    read(fd, buffer+num_read*block_size, block_size);
	    num_read++;
	    si_count++;
	  }
	  di_count++;
	}
	ti_count++;
      }
    }
    
    free(si_block);
  }
  
  return buffer;
} // end of read_file()

///////////////////////////////////////////////////////////////////////////////
//
//  print_dir() reads in a directory structure
//

static void print_dir(int fd, const struct ext2_group_desc *group,
		     const struct ext2_inode *inode)
{
  void *buffer;

  if (S_ISDIR(inode->i_mode)) {
    struct ext2_dir_entry_2 *entry;
    unsigned int size = 0;

    // read in file (directory) data
    buffer = read_file(fd, group, inode);

    // print out directory
    entry = (struct ext2_dir_entry_2 *) buffer;  // first entry
    while(size < inode->i_size) {
      if (entry->inode > 0) {
	// print out information on this entry 
	char file_name[EXT2_NAME_LEN+1];
	memcpy(file_name, entry->name, entry->name_len);
	file_name[entry->name_len] = 0; // append null to the file name
	printf("%10u %s\n", entry->inode, file_name);
      }
    
      // move on to next entry in this directory 
      size += entry->rec_len;
      entry = (void*) entry + entry->rec_len;
    }
     
    free(buffer);
  }
} // print_dir() 

///////////////////////////////////////////////////////////////////////////////
//
// ext2all.c
//
// List entries in the all directories of the floppy disk.
// Assumes no directory is more than one block in size.
//
// Nick Howe
// Emanuele Altieri
//
///////////////////////////////////////////////////////////////////////////////
//
// HEADER FILES
//

#define __LINUX_PERCPU_H
// avoid including files that generate errors

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ext2.h"

///////////////////////////////////////////////////////////////////////////////
//
//  DEFINITIONS AND MACROS
//

#define BASE_OFFSET 1024  // beginning of the super block (first group) 
#define USB_DEVICE "/dev/sda1"     // the memory stick device 

#define BLOCK_OFFSET(block) (BASE_OFFSET+(block-1)*block_size)


///////////////////////////////////////////////////////////////////////////////
//
//  GLOBALS
//

static unsigned int block_size = 0;        // to be calculated
static unsigned int ptrs_per_block = 0;    // to be calculated
static unsigned int num_inodes_per_group = 0;  // to be read in
static unsigned int num_inodes = 0;        // to be read in

///////////////////////////////////////////////////////////////////////////////
//
//  FORWARD DECLARATIONS
//

static void read_block(int, int, void*);
static void read_inode(int, const struct ext2_group_desc*, int,
		       struct ext2_inode*);
static void *read_file(int , const struct ext2_group_desc*, 
		       const struct ext2_inode*);
static void print_dir(int, const struct ext2_group_desc*,
		      const struct ext2_inode*);

///////////////////////////////////////////////////////////////////////////////
//
//  main() opens the device and initializes some of the data structures.
//

int main(void) {
  struct ext2_super_block super;
  struct ext2_group_desc *group;
  struct ext2_inode inode;
  int i;
  int fd;
  int num_groups;

  // open usb device 
  if ((fd = open(USB_DEVICE, O_RDONLY)) < 0) {
    perror(USB_DEVICE);
    exit(1);  // error while opening the floppy device 
  }

  // read super-block 
  lseek(fd, BASE_OFFSET, SEEK_SET); 
  read(fd, &super, sizeof(super));

  if (super.s_magic != EXT2_SUPER_MAGIC) {
    fprintf(stderr, "Not an Ext2 filesystem\n");
    exit(1);
  }
		
  block_size = 1024 << super.s_log_block_size;
  ptrs_per_block = (block_size/sizeof(__u32));
  num_inodes_per_group = super.s_inodes_per_group;

  // create and read in group descriptors array
  num_groups = (super.s_blocks_count+super.s_blocks_per_group-1)
    /super.s_blocks_per_group;
  if ((group = (struct ext2_group_desc*)
       malloc(num_groups*sizeof(struct ext2_group_desc))) == NULL) {
    fprintf(stderr, "Memory error\n");
    close(fd);
    exit(1);
  }
  for (i = 0; i < num_groups; i++) {
    lseek(fd,BASE_OFFSET+block_size+i*sizeof(struct ext2_group_desc),SEEK_SET);
    read(fd, group+i, sizeof(struct ext2_group_desc));
  }

  // show entries in the root directory
  read_inode(fd, group, 2, &inode);   // read inode 2 (root directory) 
  printf("   inode    listing\n---------- ----------\n");
  print_dir(fd, group, &inode);

  free(group);
  close(fd);
  exit(0);
} // end of main() 

///////////////////////////////////////////////////////////////////////////////
//
//  read_block() reads the specified block of data
//

static 
void read_block(int fd, int block_no, void *buffer)
{
  lseek(fd, BLOCK_OFFSET(block_no),SEEK_SET);
  read(fd, buffer, block_size);
} // end of read_block()

///////////////////////////////////////////////////////////////////////////////
//
//  read_inode() reads in an inode data structure
//
//  R/O:  fd, group,inode_no
//  W/O:  inode

static 
void read_inode(int fd, const struct ext2_group_desc *group, int inode_no, 
		struct ext2_inode *inode)
{
  int group_no = inode_no/num_inodes_per_group;
  //printf("group #%d; table %d\n",group_no,group[group_no].bg_inode_table);
  lseek(fd, BLOCK_OFFSET(group[group_no].bg_inode_table)+(inode_no-group_no*num_inodes_per_group-1)*sizeof(struct ext2_inode),SEEK_SET);
  read(fd, inode, sizeof(struct ext2_inode));
} // end of read_inode()

///////////////////////////////////////////////////////////////////////////////
//
//  read_file() reads in a file, allocating buffer space as necessary
//  (Note that this may not be feasible if the file is too big to fit in
//  main memory.)
//
//  R/O:  fd, group, inode

static 
void *read_file(int fd, const struct ext2_group_desc *group, 
		const struct ext2_inode *inode)
{
  void *buffer;
  __u32 *si_block, *di_block, *ti_block;
  int num_read = 0;
  int si_count, di_count, ti_count;

  // allocate space for file
  if ((buffer = malloc(block_size*inode->i_blocks)) == NULL) {
      fprintf(stderr, "Memory error\n");
      close(fd);
      exit(1);
  }

  // read direct blocks
  while ((num_read < inode->i_blocks)&&(num_read < EXT2_NDIR_BLOCKS)) {
    lseek(fd, BLOCK_OFFSET(inode->i_block[num_read]),SEEK_SET);
    read(fd, buffer+num_read*block_size, block_size);
    num_read++;
  }

  // read indirect blocks, if necessary
  if (num_read < inode->i_blocks) {
    // allocate space for indirect block pointers
    if ((si_block = malloc(3*block_size)) == NULL) {
      fprintf(stderr, "Memory error\n");
      close(fd);
      exit(1);
    }
    di_block = si_block+ptrs_per_block;
    ti_block = di_block+ptrs_per_block;
    
    // single indirect
    lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_IND_BLOCK]),SEEK_SET);
    read(fd, si_block, block_size);
    si_count = 0;
    while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
      lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
      read(fd, buffer+num_read*block_size, block_size);
      num_read++;
      si_count++;
    }

    // double indirect
    if (num_read < inode->i_blocks) {
      lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]),SEEK_SET);
      read(fd, di_block, block_size);
      di_count = 0;
      while ((num_read < inode->i_blocks)&&(di_count < ptrs_per_block)) {
	lseek(fd, BLOCK_OFFSET(di_block[di_count]),SEEK_SET);
	read(fd, si_block, block_size);
	si_count = 0;
	while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
	  lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
	  read(fd, buffer+num_read*block_size, block_size);
	  num_read++;
	  si_count++;
	}
	di_count++; 
      }
    }

    // triple indirect
    if (num_read < inode->i_blocks) {
      lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_TIND_BLOCK]),SEEK_SET);
      read(fd, ti_block, block_size);
      ti_count = 0;
      while ((num_read < inode->i_blocks)&&(ti_count < ptrs_per_block)) {
	lseek(fd,BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]),SEEK_SET);
	read(fd, di_block, block_size);
	di_count = 0;
	while ((num_read < inode->i_blocks)&&(di_count < ptrs_per_block)) {
	  lseek(fd, BLOCK_OFFSET(di_block[di_count]),SEEK_SET);
	  read(fd, si_block, block_size);
	  si_count = 0;
	  while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
	    lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
	    read(fd, buffer+num_read*block_size, block_size);
	    num_read++;
	    si_count++;
	  }
	  di_count++;
	}
	ti_count++;
      }
    }
    
    free(si_block);
  }
  
  return buffer;
} // end of read_file()

///////////////////////////////////////////////////////////////////////////////
//
//  print_dir() reads in a directory structure
//

static void print_dir(int fd, const struct ext2_group_desc *group,
		     const struct ext2_inode *inode)
{
  void *buffer;

  if (S_ISDIR(inode->i_mode)) {
    struct ext2_dir_entry_2 *entry;
    unsigned int size = 0;

    // read in file (directory) data
    buffer = read_file(fd, group, inode);

    // print out directory
    entry = (struct ext2_dir_entry_2 *) buffer;  // first entry
    while(size < inode->i_size) {
      if (entry->inode > 0) {
	// print out information on this entry 
	char file_name[EXT2_NAME_LEN+1];
	memcpy(file_name, entry->name, entry->name_len);
	file_name[entry->name_len] = 0; // append null to the file name
	printf("%10u %s\n", entry->inode, file_name);
      }
    
      // move on to next entry in this directory 
      size += entry->rec_len;
      entry = (void*) entry + entry->rec_len;
    }
     
    free(buffer);
  }
} // print_dir() 

///////////////////////////////////////////////////////////////////////////////
//
// ext2all.c
//
// List entries in the all directories of the floppy disk.
// Assumes no directory is more than one block in size.
//
// Nick Howe
// Emanuele Altieri
//
///////////////////////////////////////////////////////////////////////////////
//
// HEADER FILES
//

#define __LINUX_PERCPU_H
// avoid including files that generate errors

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ext2.h"

///////////////////////////////////////////////////////////////////////////////
//
//  DEFINITIONS AND MACROS
//

#define BASE_OFFSET 1024  // beginning of the super block (first group) 
#define USB_DEVICE "/dev/sda1"     // the memory stick device 

#define BLOCK_OFFSET(block) (BASE_OFFSET+(block-1)*block_size)


///////////////////////////////////////////////////////////////////////////////
//
//  GLOBALS
//

static unsigned int block_size = 0;        // to be calculated
static unsigned int ptrs_per_block = 0;    // to be calculated
static unsigned int num_inodes_per_group = 0;  // to be read in
static unsigned int num_inodes = 0;        // to be read in

///////////////////////////////////////////////////////////////////////////////
//
//  FORWARD DECLARATIONS
//

static void read_block(int, int, void*);
static void read_inode(int, const struct ext2_group_desc*, int,
		       struct ext2_inode*);
static void *read_file(int , const struct ext2_group_desc*, 
		       const struct ext2_inode*);
static void print_dir(int, const struct ext2_group_desc*,
		      const struct ext2_inode*);

///////////////////////////////////////////////////////////////////////////////
//
//  main() opens the device and initializes some of the data structures.
//

int main(void) {
  struct ext2_super_block super;
  struct ext2_group_desc *group;
  struct ext2_inode inode;
  int i;
  int fd;
  int num_groups;

  // open usb device 
  if ((fd = open(USB_DEVICE, O_RDONLY)) < 0) {
    perror(USB_DEVICE);
    exit(1);  // error while opening the floppy device 
  }

  // read super-block 
  lseek(fd, BASE_OFFSET, SEEK_SET); 
  read(fd, &super, sizeof(super));

  if (super.s_magic != EXT2_SUPER_MAGIC) {
    fprintf(stderr, "Not an Ext2 filesystem\n");
    exit(1);
  }
		
  block_size = 1024 << super.s_log_block_size;
  ptrs_per_block = (block_size/sizeof(__u32));
  num_inodes_per_group = super.s_inodes_per_group;

  // create and read in group descriptors array
  num_groups = (super.s_blocks_count+super.s_blocks_per_group-1)
    /super.s_blocks_per_group;
  if ((group = (struct ext2_group_desc*)
       malloc(num_groups*sizeof(struct ext2_group_desc))) == NULL) {
    fprintf(stderr, "Memory error\n");
    close(fd);
    exit(1);
  }
  for (i = 0; i < num_groups; i++) {
    lseek(fd,BASE_OFFSET+block_size+i*sizeof(struct ext2_group_desc),SEEK_SET);
    read(fd, group+i, sizeof(struct ext2_group_desc));
  }

  // show entries in the root directory
  read_inode(fd, group, 2, &inode);   // read inode 2 (root directory) 
  printf("   inode    listing\n---------- ----------\n");
  print_dir(fd, group, &inode);

  free(group);
  close(fd);
  exit(0);
} // end of main() 

///////////////////////////////////////////////////////////////////////////////
//
//  read_block() reads the specified block of data
//

static 
void read_block(int fd, int block_no, void *buffer)
{
  lseek(fd, BLOCK_OFFSET(block_no),SEEK_SET);
  read(fd, buffer, block_size);
} // end of read_block()

///////////////////////////////////////////////////////////////////////////////
//
//  read_inode() reads in an inode data structure
//
//  R/O:  fd, group,inode_no
//  W/O:  inode

static 
void read_inode(int fd, const struct ext2_group_desc *group, int inode_no, 
		struct ext2_inode *inode)
{
  int group_no = inode_no/num_inodes_per_group;
  //printf("group #%d; table %d\n",group_no,group[group_no].bg_inode_table);
  lseek(fd, BLOCK_OFFSET(group[group_no].bg_inode_table)+(inode_no-group_no*num_inodes_per_group-1)*sizeof(struct ext2_inode),SEEK_SET);
  read(fd, inode, sizeof(struct ext2_inode));
} // end of read_inode()

///////////////////////////////////////////////////////////////////////////////
//
//  read_file() reads in a file, allocating buffer space as necessary
//  (Note that this may not be feasible if the file is too big to fit in
//  main memory.)
//
//  R/O:  fd, group, inode

static 
void *read_file(int fd, const struct ext2_group_desc *group, 
		const struct ext2_inode *inode)
{
  void *buffer;
  __u32 *si_block, *di_block, *ti_block;
  int num_read = 0;
  int si_count, di_count, ti_count;

  // allocate space for file
  if ((buffer = malloc(block_size*inode->i_blocks)) == NULL) {
      fprintf(stderr, "Memory error\n");
      close(fd);
      exit(1);
  }

  // read direct blocks
  while ((num_read < inode->i_blocks)&&(num_read < EXT2_NDIR_BLOCKS)) {
    lseek(fd, BLOCK_OFFSET(inode->i_block[num_read]),SEEK_SET);
    read(fd, buffer+num_read*block_size, block_size);
    num_read++;
  }

  // read indirect blocks, if necessary
  if (num_read < inode->i_blocks) {
    // allocate space for indirect block pointers
    if ((si_block = malloc(3*block_size)) == NULL) {
      fprintf(stderr, "Memory error\n");
      close(fd);
      exit(1);
    }
    di_block = si_block+ptrs_per_block;
    ti_block = di_block+ptrs_per_block;
    
    // single indirect
    lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_IND_BLOCK]),SEEK_SET);
    read(fd, si_block, block_size);
    si_count = 0;
    while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
      lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
      read(fd, buffer+num_read*block_size, block_size);
      num_read++;
      si_count++;
    }

    // double indirect
    if (num_read < inode->i_blocks) {
      lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]),SEEK_SET);
      read(fd, di_block, block_size);
      di_count = 0;
      while ((num_read < inode->i_blocks)&&(di_count < ptrs_per_block)) {
	lseek(fd, BLOCK_OFFSET(di_block[di_count]),SEEK_SET);
	read(fd, si_block, block_size);
	si_count = 0;
	while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
	  lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
	  read(fd, buffer+num_read*block_size, block_size);
	  num_read++;
	  si_count++;
	}
	di_count++; 
      }
    }

    // triple indirect
    if (num_read < inode->i_blocks) {
      lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_TIND_BLOCK]),SEEK_SET);
      read(fd, ti_block, block_size);
      ti_count = 0;
      while ((num_read < inode->i_blocks)&&(ti_count < ptrs_per_block)) {
	lseek(fd,BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]),SEEK_SET);
	read(fd, di_block, block_size);
	di_count = 0;
	while ((num_read < inode->i_blocks)&&(di_count < ptrs_per_block)) {
	  lseek(fd, BLOCK_OFFSET(di_block[di_count]),SEEK_SET);
	  read(fd, si_block, block_size);
	  si_count = 0;
	  while ((num_read < inode->i_blocks)&&(si_count < ptrs_per_block)) {
	    lseek(fd, BLOCK_OFFSET(si_block[si_count]),SEEK_SET);
	    read(fd, buffer+num_read*block_size, block_size);
	    num_read++;
	    si_count++;
	  }
	  di_count++;
	}
	ti_count++;
      }
    }
    
    free(si_block);
  }
  
  return buffer;
} // end of read_file()

///////////////////////////////////////////////////////////////////////////////
//
//  print_dir() reads in a directory structure
//

static void print_dir(int fd, const struct ext2_group_desc *group,
		     const struct ext2_inode *inode)
{
  void *buffer;

  if (S_ISDIR(inode->i_mode)) {
    struct ext2_dir_entry_2 *entry;
    unsigned int size = 0;

    // read in file (directory) data
    buffer = read_file(fd, group, inode);

    // print out directory
    entry = (struct ext2_dir_entry_2 *) buffer;  // first entry
    while(size < inode->i_size) {
      if (entry->inode > 0) {
	// print out information on this entry 
	char file_name[EXT2_NAME_LEN+1];
	memcpy(file_name, entry->name, entry->name_len);
	file_name[entry->name_len] = 0; // append null to the file name
	printf("%10u %s\n", entry->inode, file_name);
      }
    
      // move on to next entry in this directory 
      size += entry->rec_len;
      entry = (void*) entry + entry->rec_len;
    }
     
    free(buffer);
  }
} // print_dir() 

///////////////////////////////////////////////////////////////////////////////


/*
 * Structure of the super block
 */
struct ext2_super_block {
        unsigned int   s_inodes_count;      /* Inodes count */
        unsigned int   s_blocks_count;      /* Blocks count */
        unsigned int   s_r_blocks_count;    /* Reserved blocks count */
        unsigned int   s_free_blocks_count; /* Free blocks count */
        unsigned int   s_free_inodes_count; /* Free inodes count */
        unsigned int   s_first_data_block;  /* First Data Block */
        unsigned int   s_log_block_size;    /* Block size */
        unsigned int   s_log_frag_size;     /*s Fragment size */
        unsigned int   s_blocks_per_group;  /* # Blocks per group */
        unsigned int   s_frags_per_group;   /* # Fragments per group */
        unsigned int   s_inodes_per_group;  /* # Inodes per group */
        unsigned int   s_mtime;             /* Mount time */
        unsigned int   s_wtime;             /* Write time */
        unsigned short s_mnt_count;         /* Mount count */
        unsigned short s_max_mnt_count;     /* Maximal mount count */
        unsigned short s_magic;             /* Magic signature */
        unsigned short s_state;             /* File system state */
        unsigned short s_errors;            /* Behaviour when detecting errors */
        unsigned short s_minor_rev_level;   /* minor revision level */
        unsigned int   s_lastcheck;         /* time of last check */
        unsigned int   s_checkinterval;     /* max. time between checks */
        unsigned int   s_creator_os;        /* OS */
        unsigned int   s_rev_level;         /* Revision level */
        unsigned short s_def_resuid;        /* Default uid for reserved blocks */
        unsigned short s_def_resgid;        /* Default gid for reserved blocks */
        /*
         * These fields are for EXT2_DYNAMIC_REV superblocks only.
         *
         * Note: the difference between the compatible feature set and
         * the incompatible feature set is that if there is a bit set
         * in the incompatible feature set that the kernel doesn't
         * know about, it should refuse to mount the filesystem.
         *
         * e2fsck's requirements are more strict; if it doesn't know
         * about a feature in either the compatible or incompatible
         * feature set, it must abort and not try to meddle with
         * things it doesn't understand...
         */
        unsigned int   s_first_ino;         /* First non-reserved inode */
        unsigned short s_inode_size;        /* size of inode structure */
        unsigned short s_block_group_nr;    /* block group # of this superblock */
        unsigned int   s_feature_compat;    /* compatible feature set */
        unsigned int   s_feature_incompat;  /* incompatible feature set */
        unsigned int   s_feature_ro_compat; /* readonly-compatible feature set */
        unsigned char  s_uuid[16];          /* 128-bit uuid for volume */
        char           s_volume_name[16];   /* volume name */
        char           s_last_mounted[64];  /* directory where last mounted */
        unsigned int   s_algorithm_usage_bitmap; /* For compression */
        /* 
         * Performance hints.  Directory preallocation should only
         * happen if the EXT2_COMPAT_PREALLOC flag is on.
         */
        unsigned char  s_prealloc_blocks;     /* Nr of blocks to try to preallocate*/
        unsigned char  s_prealloc_dir_blocks; /* Nr to preallocate for dirs */
        unsigned short s_padding1;
        /*
         * Journaling support valid if EXT3_FEATURE_COMPAT_HAS_JOURNAL set.
         */
        unsigned char  s_journal_uuid[16]; /* uuid of journal superblock */
        unsigned int   s_journal_inum;     /* inode number of journal file */
        unsigned int   s_journal_dev;      /* device number of journal file */
        unsigned int   s_last_orphan;      /* start of list of inodes to delete */
        unsigned int   s_hash_seed[4];     /* HTREE hash seed */
        unsigned char  s_def_hash_version; /* Default hash version to use */
        unsigned char  s_reserved_char_pad;
        unsigned short s_reserved_word_pad;
        unsigned int   s_default_mount_opts;
        unsigned int   s_first_meta_bg; /* First metablock block group */
        unsigned int   s_reserved[190]; /* Padding to the end of the block */
};


/*
 * Structure of a blocks group descriptor
 */
struct ext2_group_desc
{
        unsigned int   bg_block_bitmap;      /* Blocks bitmap block */
        unsigned int   bg_inode_bitmap;      /* Inodes bitmap block */
        unsigned int   bg_inode_table;       /* Inodes table block */
        unsigned short bg_free_blocks_count; /* Free blocks count */
        unsigned short bg_free_inodes_count; /* Free inodes count */
        unsigned short bg_used_dirs_count;   /* Directories count */
        unsigned short bg_pad;
        unsigned int   bg_reserved[3];
};


/*
 * Structure of an inode on the disk
 */
struct ext2_inode {
        unsigned short i_mode;        /* File mode */
        unsigned short i_uid;         /* Low 16 bits of Owner Uid */
        unsigned int   i_size;        /* Size in bytes */
        unsigned int   i_atime;       /* Access time */
        unsigned int   i_ctime;       /* Creation time */
        unsigned int   i_mtime;       /* Modification time */
        unsigned int   i_dtime;       /* Deletion Time */
        unsigned short i_gid;         /* Low 16 bits of Group Id */
        unsigned short i_links_count; /* Links count */
        unsigned int   i_blocks;      /* Blocks count IN DISK SECTORS*/
        unsigned int   i_flags;       /* File flags */
        unsigned int   osd1;          /* OS dependent 1 */
        unsigned int   i_block[15];   /* Pointers to blocks */
        unsigned int   i_generation;  /* File version (for NFS) */
        unsigned int   i_file_acl;    /* File ACL */
        unsigned int   i_dir_acl;     /* Directory ACL */
        unsigned int   i_faddr;       /* Fragment address */
        unsigned int   extra[3];
};

#define    EXT2_SUPER_MAGIC 0xEF53

/*
 * Type field for file mode
 */
#define    EXT2_S_IFLNK  0xA000    /* symbolic link */
#define    EXT2_S_IFREG  0x8000    /* regular file */
#define    EXT2_S_IFDIR  0x4000    /* directory */
#define    EXT2_S_IFSOCK 0xC000    /* socket */
#define    EXT2_S_IFBLK  0x6000    /* block device */
#define    EXT2_S_IFCHR  0x2000    /* character device */
#define    EXT2_S_IFIFO  0x1000    /* fifo */


/*
 * Special inode numbers
 */
/* Root inode */
#define    EXT2_ROOT_INO         2
/* First non-reserved inode for old ext2 filesystems */
#define    EXT2_GOOD_OLD_FIRST_INO 11


/*
 * Structure of a directory entry
 */
#define    EXT2_NAME_LEN 255
/*
 * The new version of the directory entry.  Since EXT2 structures are
 * stored in intel byte order, and the name_len field could never be
 * bigger than 255 chars, it's safe to reclaim the extra byte for the
 * file_type field.
 */
struct ext2_dir_entry {
        unsigned int   inode;     /* Inode number */
        unsigned short rec_len;   /* Directory entry length */
        unsigned char  name_len;  /* Name length */
        unsigned char  file_type;
        char           name[];    /* File name, up to EXT2_NAME_LEN */
};


/*
 * Ext2 directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 */
#define    EXT2_FT_UNKNOWN  0    /* Unknown File Type */
#define    EXT2_FT_REG_FILE 1    /* Regular File */
#define    EXT2_FT_DIR      2    /* Directory File */
#define    EXT2_FT_SYMLINK  7    /* Symbolic Link */
#define    EXT2_FT_CHRDEV   3    /* Character Device */
#define    EXT2_FT_BLKDEV   4    /* Block Device */
#define    EXT2_FT_FIFO     5    /* Buffer File */
#define    EXT2_FT_SOCK     6    /* Socket File */

#define    EXT2_FT_MAX      8

#endif

