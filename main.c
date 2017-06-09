#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ext2.h"

#define BASE_OFFSET 1024
#define EXT2_BLOCK_SIZE 1024
#define IMAGE "image.img"

typedef unsigned char bmap;
#define __NBITS (8 * (int)sizeof(bmap))
#define __BMELT(d) ((d) / __NBITS)
#define __BMMASK(d) ((bmap)1 << ((d) % __NBITS))
#define BM_SET(d, set) ((set[__BMELT(d)] |= __BMMASK(d)))
#define BM_CLR(d, set) ((set[__BMELT(d)] &= ~__BMMASK(d)))
#define BM_ISSET(d, set) ((set[__BMELT(d)] & __BMMASK(d)) != 0)

unsigned int block_size = 0;
static unsigned int num_inodes_per_group = 0; // to be read in
#define BLOCK_OFFSET(block) (BASE_OFFSET + (block - 1) * block_size)
// void moveBlocks(int,int);

// static void read_inode(int, const struct ext2_group_desc *, int,
//                        struct ext2_inode *);

void moveBlocks(int *xp, int *yp)
{
    int a = *xp;
    *xp = *yp;
    *yp = a;
}

int main(int argc, char *argv[])
{
    struct ext2_super_block super;
    struct ext2_group_desc *group;
    int fd, i;
    if (argc < 2)
    {
        fprintf(stderr, "Error in command line arguments.Please give the name of the imagefile\n");
        exit(1);
    }
    if ((fd = open(argv[1], O_RDONLY)) < 0)
    {
        perror(argv[1]);
        exit(1);
    }

    // read super-block to the
    lseek(fd, BASE_OFFSET, SEEK_SET);
    read(fd, &super, sizeof(super));
    if (super.s_magic != EXT2_SUPER_MAGIC)
    {
        fprintf(stderr, "Not a Ext2 filesystem\n");
        exit(1);
    }
    block_size = 1024 << super.s_log_block_size;
    num_inodes_per_group = super.s_inodes_per_group;

    int num_groups = (super.s_blocks_count + super.s_blocks_per_group - 1) / super.s_blocks_per_group;

    if ((group = (struct ext2_group_desc *)
             malloc(num_groups * sizeof(struct ext2_group_desc))) == NULL)
    {
        fprintf(stderr, "Memory error\n");
        close(fd);
        exit(1);
    }
    for (i = 0; i < num_groups; i++)
    {
        printf("Reading group # %u\n", i);
        lseek(fd, BASE_OFFSET + block_size + i * sizeof(struct ext2_group_desc), SEEK_SET);
        read(fd, group + i, sizeof(struct ext2_group_desc));
    }

    printf("\nReading from image file %s:\n"
           "Blocks count            : %u\n"
           "First non-reserved inode: %u\n"
           "Inode count %u\n"
           "Free Inode count %u\n"
           "The number of groups are: %u\n\n",
           argv[1], super.s_blocks_count,
           super.s_first_ino, super.s_inodes_count, super.s_free_inodes_count, num_groups);

    // read group descriptor
    lseek(fd, BASE_OFFSET + block_size, SEEK_SET);
    read(fd, group, sizeof(group));

    // read block bitmap
    bmap *bitmap;
    bitmap = malloc(block_size);
    lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
    read(fd, bitmap, block_size);
    int fr = 0;
    int nfr = 0;
    printf("Free block bitmap:\n");
    for (i = 0; i < super.s_blocks_count; i++)
    {
        if (BM_ISSET(i, bitmap))
        {
            printf("+"); // in use
            nfr++;
        }
        else
        {
            printf("-"); // empty
            fr++;
        }
    }

    printf("\nFree blocks count       : %u\n"
           "Non-Free block count    : %u\n",
           fr, nfr);
    free(bitmap);

    int inodes[super.s_inodes_count * 15];
    int block[super.s_inodes_count * 15];
    struct ext2_inode *inodeValuesfrag[super.s_inodes_count * 15];
    int fd_block[super.s_inodes_count * 15];
    int block_a[super.s_inodes_count * 15];

    struct ext2_inode inode;
    int j, temp;
    int count = 0;
    printf("The number of nodes per group is %u and size of each node is %u\n\n", super.s_inodes_per_group, super.s_inode_size);
    for (j = 11; j < super.s_inodes_count; j++)
    {
        int group_no = j / num_inodes_per_group;
        // printf("group #%d; table %d\n", group_no, group[group_no].bg_inode_table);
        lseek(fd, BLOCK_OFFSET(group[group_no].bg_inode_table) + (sizeof(struct ext2_inode) * j), SEEK_SET);
        read(fd, &inode, sizeof(struct ext2_inode));
        // printf("\nThe size of the %u innode is %d\n", j, inode.i_size);
        // printf("Reading inode\n"
        //        "Size     : %u bytes\n"
        //        "Blocks   : %u\n",
        //        inode.i_size,
        //        inode.i_blocks); // in number of sectors. A disk sector is 512 bytes.
        int i;
        for (i = 0; i < 15; i++)
        {
            // if (i < 12) // direct blocks
            //     printf("Block %2u : %u\n", i, inode.i_block[i]);
            // else if (i == 12) // single indirect block
            //     printf("Single   : %u\n", inode.i_block[i]);
            // else if (i == 13) // double indirect block
            //     printf("Double   : %u\n", inode.i_block[i]);
            // else if (i == 14) // triple indirect block
            //     printf("Triple   : %u\n", inode.i_block[i]);
            if (inode.i_block[i] > 0)
            {
                printf("%u %u\n", j + 1, inode.i_block[i]);
                inodes[count] = j; // the inode
                block[count] = i;  // the  block in the inode
                inodeValuesfrag[count] = &inode;
                fd_block[count] = fd;
                block_a[count] = inode.i_block[i];
                count++;
            }
        }
    }
    for (i = 0; i < count - 1; i++)
        for (j = 0; j < count - i - 1; j++)
        {
            if (inodeValuesfrag[i]->i_block[block[j]] > inodeValuesfrag[i]->i_block[block[j + 1]])
            {
                int group_no = j / num_inodes_per_group;
                lseek(fd, BLOCK_OFFSET(group[0].bg_inode_table) + (sizeof(struct ext2_inode) * inodes[i]), SEEK_SET);
                read(fd, &inode, sizeof(struct ext2_inode));
                moveBlocks(&inodeValuesfrag[i]->i_block[block[j]], &inodeValuesfrag[i]->i_block[block[j + 1]]);
            }
        }
    for (i = 0; i < count - 1; i++)
        for (j = 0; j < count - i - 1; j++)
        {
            if (block_a[j] > block_a[j + 1])
            {
                moveBlocks(&block_a[j], &block_a[j + 1]);
            }
        }
    printf("################################################\n");

    for (i = 0; i < count; i++)
    {
        int group_no = j / num_inodes_per_group;
        lseek(fd, BLOCK_OFFSET(group[0].bg_inode_table) + (sizeof(struct ext2_inode) * inodes[i]), SEEK_SET);
        read(fd, &inode, sizeof(struct ext2_inode));
        printf("%u %u\n", inodes[i] + 1, block_a[i]);
    }
    close(fd);
    return 0;
}
