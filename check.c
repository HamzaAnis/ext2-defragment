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
