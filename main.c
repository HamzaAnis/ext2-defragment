#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ext2.h"

#define BASE_OFFSET 1024
#define EXT2_BLOCK_SIZE 1024
#define IMAGE "image.img"

typedef unsigned char bmap;
#define __NBITS (8 * (int) sizeof (bmap))
#define __BMELT(d) ((d) / __NBITS)
#define __BMMASK(d) ((bmap) 1 << ((d) % __NBITS))
#define BM_SET(d, set) ((set[__BMELT (d)] |= __BMMASK (d)))
#define BM_CLR(d, set) ((set[__BMELT (d)] &= ~__BMMASK (d)))
#define BM_ISSET(d, set) ((set[__BMELT (d)] & __BMMASK (d)) != 0)

unsigned int block_size = 0;
#define BLOCK_OFFSET(block) (BASE_OFFSET + (block-1)*block_size)

int main(void)
{
    struct ext2_super_block super;
    struct ext2_group_desc group;
    int fd,i;

    if ((fd = open(IMAGE, O_RDONLY)) < 0) {
        perror(IMAGE);
        exit(1);
    }

    // read super-block
    lseek(fd, BASE_OFFSET, SEEK_SET);
    read(fd, &super, sizeof(super));
    if (super.s_magic != EXT2_SUPER_MAGIC) {
        fprintf(stderr, "Not a Ext2 filesystem\n");
        exit(1);
    }
    block_size = 1024 << super.s_log_block_size;

    printf("Reading from image file " IMAGE ":\n"
           "Blocks count            : %u\n"
           "First non-reserved inode: %u\n",
           super.s_blocks_count,
           super.s_first_ino);

    // read group descriptor
    lseek(fd, BASE_OFFSET + block_size, SEEK_SET);
    read(fd, &group, sizeof(group));
    
    // read block bitmap
    bmap *bitmap;
    bitmap = malloc(block_size);
    lseek(fd, BLOCK_OFFSET(group.bg_block_bitmap), SEEK_SET);
    read(fd, bitmap, block_size);
    int fr = 0;
    int nfr = 0;
    printf("Free block bitmap:\n");
    for (i = 0; i < super.s_blocks_count; i++){
        if (BM_ISSET(i,bitmap)){
            printf("+");    // in use
            nfr++;
        }
        else{
            printf("-");    // empty
            fr++;
        }
    }
    printf("\n");
    printf("Free blocks count       : %u\n"
           "Non-Free block count    : %u\n"
           , fr, nfr);
    free(bitmap);

    // read root inode
    struct ext2_inode inode;
    lseek(fd, BLOCK_OFFSET(group.bg_inode_table)+sizeof(struct ext2_inode), SEEK_SET);
    read(fd, &inode, sizeof(struct ext2_inode));
    printf("Reading root inode\n"
           "Size     : %u bytes\n"
           "Blocks   : %u\n",
           inode.i_size,
           inode.i_blocks); // in number of sectors. A disk sector is 512 bytes.
    for( i=0; i < 15; i++){
        if (i < 12)         // direct blocks
            printf("Block %2u : %u\n", i, inode.i_block[i]);
        else if (i == 12)     // single indirect block
            printf("Single   : %u\n", inode.i_block[i]);
        else if (i == 13)    // double indirect block
            printf("Double   : %u\n", inode.i_block[i]);
        else if (i == 14)    // triple indirect block
            printf("Triple   : %u\n", inode.i_block[i]);
    }
    
    close(fd);
    return 0;
}
