#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <linux/fs.h>
#include <string.h>
#include "../kern/ux_fs.h"

int main(int argc, char* argv[])
{
	struct ux_dirent dir;
	struct ux_superblock sb;
	struct ux_inode inode;	

	time_t tm;
	off_t  nsectors = UX_MAXBLOCKS;
	int    devfd, error, i;
	int    mapblocks;
	char   block[UX_BSIZE];

	if(argc != 2){
		fprintf(stderr, "uxmkfs:needs device name\n");
		_exit(1);
	}
	
	devfd = open(argv[1], O_WRONLY);	
	if(devfd < 0){
		fprintf(stderr, "uxmkfs:failed to open device\n");
		_exit(1);
	}

	error = lseek(devfd, (off_t)(nsectors*UX_BSIZE), SEEK_SET);
	if(error == -1){
		fprintf(stderr, "uxmkfs:can not create file system of specified size\n");
		_exit(1);
	}
	lseek(devfd, 0, SEEK_SET);
	
	/*
	fill in the super block, then write it to the first block of device
	*/
	
	sb.s_magic = UX_MAGIC;
	sb.s_mode = UX_FSCLEAN;
	sb.s_nifree = UX_MAXFILES - 2;
	sb.s_nbfree = UX_MAXBLOCKS - 2;

	/*
	first 2 nodes are in use. 
	Inodes 0 is not used by anything, 2 is the root directory
	*/

	sb.s_inode[0] = UX_INODE_INUSE;
	sb.s_inode[1] = UX_INODE_INUSE;

	/*
	rest nodes are marked as unused
	*/
	for(i = 2; i < UX_MAXFILES; i++){
		sb.s_inode[i] = UX_INODE_FREE;
	}

	/*
	the first 2 blocks are allocated for the entries of the root directory
	*/

	sb.s_block[0] = UX_BLOCK_INUSE;
	sb.s_block[1] = UX_BLOCK_INUSE;

	/*
	the rest blocks are marked as free
	*/

	for(i = 2; i < UX_MAXBLOCKS; i++){
		sb.s_block[i] = UX_BLOCK_FREE;
	}

	write(devfd, &sb, sizeof(struct ux_superblock));

	/*
	the root directory inode must be initialized
	*/
	time(&tm);
	memset((void*)&inode, 0, sizeof(struct ux_inode));
	inode.i_mode = S_IFDIR | 0755;
	inode.i_nlink = 2;
	inode.i_atime = tm;
	inode.i_mtime = tm;
	inode.i_ctime = tm;
	
	inode.i_gid = 0;
	inode.i_uid = 0;
	inode.i_size = UX_BSIZE;
	inode.i_blocks = 1;
	inode.i_addr[0] = UX_FIRST_DATA_BLOCK;

	lseek(devfd, UX_INODE_BLOCK * UX_BSIZE + 1024, SEEK_SET );
	write(devfd, (char*)&inode, sizeof(struct ux_superblock));

	/* fill in the directory for root */

	lseek(devfd, UX_FIRST_DATA_BLOCK * UX_BSIZE, SEEK_SET);
	memset((void*)&block, 0, UX_BSIZE);
	write(devfd, block, UX_BSIZE);
	lseek(devfd, UX_FIRST_DATA_BLOCK * UX_BSIZE, SEEK_SET);
	
	memset(&dir, 0, sizeof(dir));
	dir.d_ino = 2;
	strcpy(dir.d_name, ".");
	write(devfd, (char*)&dir, sizeof(struct ux_dirent));
	
	memset(&dir, 0, sizeof(dir));
	dir.d_ino = 2;
	strcpy(dir.d_name, "..");
	write(devfd, (char*)&dir, sizeof(struct ux_dirent));
}
