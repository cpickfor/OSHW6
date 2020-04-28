#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

int inode_blocks;
int *allocate_bitmap;
int mounted = 0;


struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

void update_Bmap(){
	union fs_block block;
	union fs_block inodeBlk;

	//read disk
	for(int i = 0; i < disk_size(); i++){
		disk_read(i,block.data); //reading blocks

	}

}

int fs_format()
{
	return 0;
}

void fs_debug()
{
	union fs_block block;
	union fs_block temp;
	union fs_block indirect;
	
	disk_read(0,block.data);

	printf("superblock:\n");
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);
	
    	for(int i = 1; i <= block.super.ninodeblocks; i++) {	//loop through inode blocks
        disk_read(i,temp.data);
        
        for(int j = 0; j < INODES_PER_BLOCK; j++) {	//loop through inodes
            if(temp.inode[j].isvalid == 1) {	//print if inode is valid
                int inumber = (i-1)*INODES_PER_BLOCK + j;
                printf("inode %d\n", inumber);
                printf("    size: %d bytes\n", temp.inode[j].size);
		if(temp.inode[j].size == 0){
			return;
		}
                
		printf("    direct blocks:");
                for(int k = 0; k < POINTERS_PER_INODE; k++) {	//loop through direct pointers
                    if(temp.inode[j].direct[k] != 0) {
                        printf(" %d", temp.inode[j].direct[k]);
                    }
                }
                printf("\n");
		
                if(temp.inode[j].indirect != 0) {	//inderect block
                    printf("    indirect block: %d\n", temp.inode[j].indirect);
                    disk_read(temp.inode[j].indirect, indirect.data);
		
                    printf("    indirect data blocks:");
                    for(int x = 0; x < POINTERS_PER_BLOCK; x++) {	//loop through indirect data blocks
                        if(indirect.pointers[x] != 0) {
                            printf(" %d", indirect.pointers[x]);
                        }
                    }
                    printf("\n");
                }
            }
        }
    }
}

int fs_mount()
{
	//Read 0 block from disk
	union fs_block block;
	disk_read(0,block.data);
	//Check for magic number
	if(block.super.magic != FS_MAGIC) return 0;
	//say if mounted
	mounted = 1;
	//Allocate bitmap (calloc)
	allocate_bitmap = calloc(block.super.nblocks,sizeof(int));
	if(!allocate_bitmap) return 0;
	inode_blocks = block.super.ninodeblocks;
	//Update bitmap function
	update_Bmap();


	return 0;
}

int fs_create()
{
	return 0;
}

int fs_delete( int inumber )
{
	union fs_block block;

	if(inumber > inode_blocks*INODES_PER_BLOCK - 1 || inumber < 0) return 0; //impossible inodes fails automatically

	//read block
	int iblk = inumber/INODES_PER_BLOCK + 1; //get block number for inode
	int localIndex = inumber%INODES_PER_BLOCK; //get local index for inode
	

	//iterate through pointers
	//update bitmap for specific block to be 0 when pointer found
	return 0;
}

int fs_getsize( int inumber )
{
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
