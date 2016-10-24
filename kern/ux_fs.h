extern struct address_space_operations ux_aops;
extern struct inode_operations ux_file_inops;
extern struct inode_operations ux_dir_inops;
extern struct file_operations ux_file_operations;
extern struct file_operations ux_dir_operations;

#define UX_NAMELEN 28
#define UX_DIRS_PER_BLOCK 16
#define UX_DIRECT_BLOCKS  16
#define UX_MAXFILES 32
#define UX_MAXBLOCKS 1024
#define UX_FIRST_DATA_BLOCK 50
#define UX_BSIZE 512
#define UX_BSIZE_BITS 9
#define UX_MAGIC 0x58494e55
#define UX_INODE_BLOCK 8
#define UX_ROOT_NO 2

struct ux_superblock{
	__u32 s_magic;
	__u32 s_mode;
	__u32 s_nifree;
	__u32 s_inode[UX_MAXFILES];
	__u32 s_nbfree;
	__u32 s_block[UX_MAXBLOCKS];
};

struct ux_inode{
	__u32 i_mode;
	__u32 i_nlink;
	__u32 i_atime;
	__u32 i_mtime;
	__u32 i_ctime;
	__u32 i_uid;
	__u32 i_gid;
	__u32 i_size;
	__u32 i_blocks;
	__u32 i_addr[UX_DIRECT_BLOCKS];
};


/*allocation flags*/
#define UX_INODE_FREE 0
#define UX_INODE_INUSE 1
#define UX_BLOCK_FREE 0
#define UX_BLOCK_INUSE 1

/*file system flags*/
#define UX_FSCLEAN 0
#define UX_FSDIRTY 1

struct ux_dirent{
	__u32 d_ino;
	char d_name[UX_NAMELEN];
};

#define UX_DIRENT_SIZE 32
struct ux_fs{
	struct ux_superblock *u_sb;
	struct buffer_head *u_sbh;
};

#ifdef __KERNEL__

struct uxfs_inode_info{
	struct inode vfs_inode;
	__u32 i_blocks;
	__u32 i_addr[UX_DIRECT_BLOCKS];
};

static inline struct uxfs_inode_info *UXFS_I(struct inode *inode)
{
	return container_of(inode, struct uxfs_inode_info, vfs_inode);
}

extern ino_t ux_ialloc(struct super_block *);
//extern struct buffer_head* ux_find_entry(struct inode *, char *, struct ux_dirent**);
__u32 ux_block_alloc(struct super_block *);
int ux_get_block(struct inode *inode, sector_t block, struct buffer_head *bh_result, int create);
extern int ux_prepare_chunk(struct page *page, loff_t pos, unsigned len);
#endif
