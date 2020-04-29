#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>

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

struct FileSystem {
	struct fs_superblock sb;
	bool * free_blocks;
};

struct FileSystem * fs;

void update_Bmap(){
	union fs_block block;
	union fs_block indirect_block;

	for (int i = 0; i < disk_size(); i++) {
		//read
		disk_read(i, block.data);
		//check superblock magic number
		if (!i) {
			allocate_bitmap[0] = (block.super.magic == FS_MAGIC) ? 1 : 0;
		}
		else if (i <= inode_blocks) {//inode blocks
			//loop through inodes
			int foundValid = 0;
			for (int j = 0; j < INODES_PER_BLOCK; j++) {
				//check validity
				if (block.inode[j].isvalid) {
					allocate_bitmap[i] = 1;
					foundValid = 1;
					//direct pointers
					for (int k = 0; k < POINTERS_PER_INODE; k++) {
						if(block.inode[j].direct[k]) allocate_bitmap[block.inode[j].direct[k]] = 1;
					}
					//indirect pointer
					if (block.inode[j].indirect) {
						//read
						disk_read(block.inode[j].indirect, indirect_block.data);
						for (int m = 0; m < POINTERS_PER_BLOCK; m++) {
							if(indirect_block.pointers[m]) allocate_bitmap[indirect_block.pointers[m]] = 1;
						}
					}
				}
			}
			if(!foundValid) allocate_bitmap[i] = 0;
		}
	}
}

void fs_save_inode(int inode_number, struct fs_inode *node)
{
	union fs_block block;
	int block_number = inode_number / INODES_PER_BLOCK + 1;
	int inode_index = inode_number % INODES_PER_BLOCK;
	disk_read(block_number,block.data);
	if(!block.inode[inode_index].isvalid) return;
	block.inode[inode_index] = *node;	//expression must have pointer-to-object type
	disk_write(block_number,block.data);
	return;
}

void inode_load( int inumber, struct fs_inode *inode) {
	union fs_block block;
    int block_number = inumber / INODES_PER_BLOCK + 1;
    int inode_index = inumber % INODES_PER_BLOCK;
    disk_read(block_number, block.data);
    *inode = block.inode[inode_index];
}

int fs_read(int inode_number, char *data, int length, int offset)
{
	if(!mounted) {
        printf("Filesystem is not mounted\n");
        return 0;
    }
	if(length == 0){return 0;}
	union fs_block block;
	int block_number = inode_number / INODES_PER_BLOCK + 1;
	int inode_index = inode_number % INODES_PER_BLOCK;
	disk_read(block_number,block.data);
	if(!block.inode[inode_index].isvalid) return -1;

	struct fs_inode node = block.inode[inode_index];
	if(length+offset > node.size){return -1;}

	int bytes_read = 0;
	int bytes_seen = 0;
	//go through all direct pointers
	for(int i = 0; i < POINTERS_PER_INODE; i++)
	{
		int block_num = node.direct[i];
		union fs_block data_block;
		disk_read(block_num,data_block.data); //read data block
		//read data block
		for(int j = 0; j < DISK_BLOCK_SIZE; j++)
		{
			if(bytes_seen >= offset)
			{
				data[bytes_read] = data_block.data[i];
				bytes_read++;
				if(bytes_read == length){return bytes_read;}
			}
			bytes_seen++;
		}
	}
	//go through indirect pointers
	int indirect_num = node.indirect;
	union fs_block indirect_block;
	disk_read(indirect_num,indirect_block.data);
	for(int i = 0; i < POINTERS_PER_BLOCK; i++)
	{
		int data_block_num = indirect_block.pointers[i];
		union fs_block in_data_block;
		disk_read(data_block_num,in_data_block.data);
		for(int j = 0; j < DISK_BLOCK_SIZE; j++)
		{
			if(bytes_seen >= offset)
			{
				data[bytes_read] = in_data_block.data[i];
				bytes_read++;
				if(bytes_read == length){return bytes_read;}
			}
			bytes_seen++;
		}
	}



	return bytes_read;


}

int fs_create()
{
	if(!mounted) {
        printf("Filesystem is not mounted\n");
        return 0;
    }


	struct fs_inode node;
	union fs_block block;
	node.size = 0;
	node.isvalid = 1;
	//check through every block
	for(int i = 1; i < disk_size(); i++)
	{
		//ready block
		disk_read(i,block.data);
		//check through every inode
		for(int j = 0; j < INODES_PER_BLOCK; j++)
		{
			//if inode is not valid then assign to created node
			if(!block.inode[j].isvalid)
			{
				block.inode[j] = node;
				return i*INODES_PER_BLOCK+j;
			}
		}
	}
	return 0;
}



void initialize_free_block_bitmap(struct FileSystem * fs){

	// check that filesytem is valid	
	if(fs->sb.magic != FS_MAGIC){
		printf("fs: Error initializing free block bitmap.\n");
		exit(1);
	}

	// allocate free block bitmap
	fs->free_blocks = calloc(fs->sb.nblocks,sizeof(bool));

	// give error if allocating memory fails
	if(!fs->free_blocks) {
		printf("fs: Error allocating memory for free block bitmap.\n");
		exit(1);
	}
}

ssize_t allocate_free_block(struct FileSystem * fs){

	// look for free block
	for (ssize_t i = 0; i < fs->sb.nblocks; i++){
		if(!fs->free_blocks[i]){
			fs->free_blocks[i] = true;
			return i;
		}
	}

	// no free blocks	
	return -1;
}


int fs_format()
{
	//check if mounted
	if(mounted) {
        printf("Disk is already mounted\n");
        return 0;
    }
	
	union fs_block block;
	disk_read(0,block.data);
	int nblocks = disk_size();
	int ninodeblocks = ceil(nblocks/10);
	if(ninodeblocks == 0) ninodeblocks = 1;

	for(int i = 1; i <= block.super.ninodeblocks; i++) {
        union fs_block inode;
        disk_read(i,inode.data);
        for(int j = 0; j < INODES_PER_BLOCK; j++) {
            inode.inode[j].isvalid = 0;
        }
        disk_write(i,inode.data);
    }

	union fs_block superblock;
	superblock.super.magic = FS_MAGIC;
	superblock.super.nblocks = nblocks;
	superblock.super.ninodeblocks = ninodeblocks;
	superblock.super.ninodes = (ninodeblocks * INODES_PER_BLOCK);
    disk_write(0,superblock.data);

    return 1;
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
                printf("inode %d:\n", inumber);
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
	mounted = 1;

	return 1;
}



int fs_delete( int inumber )
{
	if(!mounted) {
        printf("Filesystem is not mounted\n");
        return 0;
    }
	union fs_block block;
	union fs_block in_block;

	if(inumber > inode_blocks*INODES_PER_BLOCK - 1 || inumber < 0) return 0; //impossible inodes fails automatically

	//find location
	int blk = inumber/INODES_PER_BLOCK + 1; //get block number for inode
	int localIndex = inumber%INODES_PER_BLOCK; //get local index for inode

	//read block
	disk_read(blk, block.data);

	//Check validity
	if(!block.inode[localIndex].isvalid) return 0;
	//iterate through direct pointers
	for(int i=0;i<POINTERS_PER_INODE;i++){
		if(!block.inode[localIndex].direct[i]) continue;
		allocate_bitmap[block.inode[localIndex].direct[i]] = 0;
	}
	//iterate through indirect pointers
	if(block.inode[localIndex].indirect){	
		disk_read(block.inode[localIndex].indirect, in_block.data);
		for(int j=0; j<POINTERS_PER_BLOCK; j++){
			if(!in_block.pointers[j]) continue;
			allocate_bitmap[in_block.pointers[j]] = 0;
		}
	}

	//size update
	block.inode[localIndex].size = 0;

	//invalidate inode
	block.inode[localIndex].isvalid = 0;

	//Check all inodes in inode block for any valid inode
	int foundValidInode = 0;
	for(int k=0; k<INODES_PER_BLOCK; k++){
		if(block.inode[k].isvalid){
			foundValidInode = 1;
			break;
		}
	}

	//invalidate block
	if(!foundValidInode) allocate_bitmap[blk] = 0;
	
	//write to disk
	disk_write(blk, block.data);
	
	return 1;
}

int fs_getsize( int inumber )
{
	if(!mounted) {
        printf("Filesystem is not mounted\n");
        return 0;
    }
	union fs_block block;

	if(inumber > fs->sb.ninodes){
		printf("fs: Invalid inode number.\n");
		return -1;
	}

	int block_of_inode = (inumber/INODES_PER_BLOCK) + 1;

	disk_read(block_of_inode, block.data);

	int inode_offset = (inumber % INODES_PER_BLOCK) - 1;

	if(block.inode[inode_offset].isvalid == 0){
		printf("fs: inode is invalid.\n");
		return -1;
	}	

	return block.inode[inode_offset].size;
}


int fs_write( int inumber, const char *data, int length, int offset )
{	
	if(!mounted) {
        printf("Filesystem is not mounted\n");
        return 0;
    }
	union fs_block block;
	union fs_block temp;
	union fs_block indirect;

	if(inumber > fs->sb.ninodes){
		printf("fs: Invalid inode number.\n");
		return 0;
	}

	int block_of_inode = (inumber / INODES_PER_BLOCK) + 1;

	disk_read(block_of_inode, block.data);

	int inode_offset = inumber % INODES_PER_BLOCK;


	struct fs_inode ind = block.inode[inode_offset];  // TODO dont forget to write to disk
	size_t bytes_written = 0;
	
	if(ind.isvalid == 0){
		ind.isvalid = 1;
		ind.size    = 0;

		int free_block;
		int numblocks          = (length / DISK_BLOCK_SIZE) + 1;
		int direct_ptrs_used   = 0;
		int indirect_ptrs_used = 0;
		bool indirect_used      = false;

		while(numblocks > 0){

			if(direct_ptrs_used < POINTERS_PER_INODE){
				
				free_block = allocate_free_block(fs);
				if(free_block == -1){
					printf("fs: Cannot allocate a block.\n");
					return 0;
				}

				ind.direct[direct_ptrs_used] = free_block;

				if(numblocks-1 == 0){
					for(int i = 0; i < length - bytes_written; i++){
						temp.data[i] = data[i + offset + bytes_written];
					}		
					disk_write(free_block,temp.data);
					bytes_written += length - bytes_written;
					ind.size += length - bytes_written;	
				}else{
					for(int i = 0; i < DISK_BLOCK_SIZE; i++){
						temp.data[i] = data[i + offset + bytes_written];
					}		
					disk_write(free_block,temp.data);
					bytes_written += DISK_BLOCK_SIZE;
					ind.size += DISK_BLOCK_SIZE;
				}
				numblocks--;
				direct_ptrs_used++;

			}else if (indirect_ptrs_used < POINTERS_PER_BLOCK){

				if (!indirect_used)	{
					free_block = allocate_free_block(fs);
					if(free_block == -1){
						printf("fs: Cannot allocate a block.\n");
						return 0;
					}
					ind.indirect = free_block; 
				}
				
				free_block = allocate_free_block(fs);
				if(free_block == -1){
					printf("fs: Cannot allocate a block.\n");
					return 0;
				}
				
				indirect.pointers[indirect_ptrs_used] = free_block;

				if(numblocks-1 == 0){
					for(int i = 0; i < length - bytes_written; i++){
						temp.data[i] = data[i + offset + bytes_written];
					}		
					disk_write(free_block,temp.data);
					bytes_written += length - bytes_written;
					ind.size += length - bytes_written;	
					
				}else{
					for(int i = 0; i < DISK_BLOCK_SIZE; i++){
						temp.data[i] = data[i + offset + bytes_written];
					}		
					disk_write(free_block,temp.data);
					bytes_written += DISK_BLOCK_SIZE;
					ind.size += DISK_BLOCK_SIZE;
				}
				
				indirect_ptrs_used++;
				numblocks--;
			}else{
				printf("All of inodes used\n");
				return bytes_written; // TODO return 0 on error
			}

			numblocks--;	
		}
		
		block.inode[inode_offset] = ind;
		disk_write(block_of_inode,block.data);

		if(indirect_used) disk_write(ind.indirect, indirect.data);

	}else{
		// TODO can inode be valid ?
	}
		
	
	
	return 0;
}
