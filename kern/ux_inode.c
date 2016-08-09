#include <linux/module.h>
#include <linux/dcache.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/vfs.h>
#include "ux_fs.h"


static struct kmem_cache *uxfs_inode_cachep;

static struct inode* ux_alloc_inode(struct super_block *sb)
{
	struct uxfs_inode_info* ui;
	printk("ux_alloc_inode \n");
	ui = kmem_cache_alloc(uxfs_inode_cachep, GFP_KERNEL);
	if (!ui)
		return NULL;
	return &ui->vfs_inode; 
}

static struct buffer_head* ux_find_entry(struct inode *dir, char *name, struct ux_dirent **res_dir)
{
	struct super_block *sb = dir->i_sb;
	struct uxfs_inode_info *ui = UXFS_I(dir);
	struct buffer_head *bh = NULL;
	struct ux_dirent   *dirent;
	int    i, blk = 0;
	printk("ux_find_entry: name=%s\n", name);
	for (blk=0 ; blk < dir->i_blocks ; blk++) {
		bh = sb_bread(sb, ui->i_addr[blk]);
		dirent = (struct ux_dirent *)bh->b_data;
		for (i=0 ; i < UX_DIRS_PER_BLOCK ; i++) {
			if (strcmp(dirent->d_name, name) == 0) {
				*res_dir = dirent;
				return bh;
			}
			dirent++;
		}
	}

	if (bh)
		brelse(bh);
	return NULL;
}

void ux_read_inode(struct inode *inode)
{
	struct buffer_head	  *bh;
	struct ux_inode		  *ui;
	unsigned long		  ino = inode->i_ino;
	int			  block;

	printk("ux_read_inode ino = %lu \n", ino);
	if (ino < UX_ROOT_NO || ino > UX_MAXFILES) {
		printk("uxfs: Bad inode number %lu\n", ino);
		return;
	}

	/*
	 * Note that for simplicity, there is only one 
	 * inode per block!
	 */

	block = UX_INODE_BLOCK + ino;
	bh = sb_bread(inode->i_sb, block);
	if (!bh) {
		printk("Unable to read inode %lu\n", ino);
		return;
	}

	ui = (struct ux_inode *)(bh->b_data);

	printk("ux_read_inode imode = %lu\n", (long unsigned int)ui->i_mode);
	inode->i_mode = ui->i_mode;
	if (ui->i_mode & S_IFDIR) {
		inode->i_mode |= S_IFDIR;
		inode->i_op = &ux_dir_inops;
		inode->i_fop = &ux_dir_operations;
	} else if (ui->i_mode & S_IFREG) {
		inode->i_mode |= S_IFREG;
		inode->i_op = &ux_file_inops;
		inode->i_fop = &ux_file_operations;
		inode->i_mapping->a_ops = &ux_aops;
	}
	
	i_uid_write(inode, ui->i_uid);
	i_gid_write(inode, ui->i_gid);
	set_nlink(inode, ui->i_nlink);
	printk("ui i_size = %u, ui i_blocks = %u\n", ui->i_size, ui->i_blocks);
	
	inode->i_size = ui->i_size;
	inode->i_blocks = ui->i_blocks;
	inode->i_blkbits = 9;
	inode->i_atime.tv_sec = ui->i_atime;
	inode->i_mtime.tv_sec = ui->i_mtime;
	inode->i_ctime.tv_sec = ui->i_ctime;
	printk("inode = %p\n", inode);
	UXFS_I(inode)->i_blocks = ui->i_blocks;
	memcpy(UXFS_I(inode)->i_addr, ui->i_addr, sizeof(ui->i_addr));
	printk("ui blocks = %u\n", UXFS_I(inode)->i_blocks);
	brelse(bh);
}

static struct ux_inode *find_inode(struct super_block* sb, u16 ino, struct buffer_head** p)
{
	printk("ux_find_inode %lu\n", (unsigned long)ino);
	*p = sb_bread(sb, ino + UX_INODE_BLOCK); 
	if (!*p)
		printk("unable to read inode\n");
	return (struct ux_inode*)((*p)->b_data);
}

static int ux_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	unsigned long ino = inode->i_ino;
	struct ux_inode *ui;
	struct uxfs_inode_info *info = UXFS_I(inode);
	struct buffer_head *bh;
	__u32 blk;

	printk("uxfs: ux_write_inode, ino = %lu, inode->i_mode = %lu, isize = %d, blocks=%d, inodeblocks=%d\n", ino, (long unsigned int)inode->i_mode, (unsigned int)inode->i_size, (unsigned int)info->i_blocks, (int)inode->i_blocks);
	if(ino < UX_ROOT_NO || ino > UX_MAXFILES){
		printk("uxfs: Bad inode number %lu\n", ino);
		return -1;
	}
	
	ui = find_inode(inode->i_sb, inode->i_ino, &bh);
	if (IS_ERR(ui))
		return PTR_ERR(ui);
	
	blk = UX_INODE_BLOCK + ino;
	ui->i_mode = inode->i_mode;
	ui->i_nlink = inode->i_nlink;
	ui->i_atime = inode->i_atime.tv_sec;
	ui->i_mtime = inode->i_mtime.tv_sec;
	ui->i_ctime = inode->i_ctime.tv_sec;
	ui->i_uid = i_uid_read(inode);
	ui->i_gid = i_gid_read(inode);
	ui->i_size = inode->i_size;
	ui->i_blocks = (ui->i_size + UX_BSIZE - 1)/UX_BSIZE;
	memcpy(ui->i_addr, info->i_addr, sizeof(ui->i_addr));
	memcpy(bh->b_data, ui, sizeof(struct ux_inode));
	mark_buffer_dirty(bh);
	brelse(bh);
	return 0;
}

static void ux_evict_inode(struct inode *inode)
{
	struct buffer_head *bh;
	struct ux_inode* ui;
	struct super_block *sb = inode->i_sb;
	struct ux_fs *info = (struct uxfs_fs_info*)sb->s_fs_info;
	struct ux_superblock *usb = info->u_sb;
	int i = 0;

	printk("evict inode = %p, inode->i_nlink = %u inode->i_ino = %u\n", inode, inode->i_nlink, (unsigned int)inode->i_ino);
	truncate_inode_pages_final(&inode->i_data);
	invalidate_inode_buffers(inode);
	clear_inode(inode);
	
	if (inode->i_nlink)
		return;
	
	ui = find_inode(sb, inode->i_ino, &bh);
	if (IS_ERR(ui))
		return;

	usb->s_nifree++;
	usb->s_inode[inode->i_ino] = UX_INODE_FREE;	
	for(i = 0; i < ui->i_blocks; i++){
		usb->s_block[ui->i_addr[i] - UX_FIRST_DATA_BLOCK] = UX_BLOCK_FREE;
		usb->s_nbfree++;
	}
	mark_buffer_dirty(info->u_sbh);

	memset(ui, 0, sizeof(struct ux_inode));
	mark_buffer_dirty(bh);
	brelse(bh);

}

void ux_put_super(struct super_block* s)
{
	struct ux_fs *fs = (struct ux_fs*)s->s_fs_info;
	if (!fs)
		return;
	brelse(fs->u_sbh);
	printk("ux_put_super\n");
	kfree(fs);
	s->s_fs_info = NULL;
}

int ux_statfs(struct dentry* dentry, struct kstatfs* buf)
{
	struct super_block* s;
	struct ux_fs *fs;
	struct ux_superblock *usb;
	u64 id ;
	
	printk("ux_statfs\n");
	s = dentry->d_sb;
	fs = (struct ux_fs*)s->s_fs_info;
	usb = fs->u_sb;
	id = huge_encode_dev(s->s_bdev->bd_dev);

	buf->f_type = UX_MAGIC;
	buf->f_bsize = UX_BSIZE;
	buf->f_blocks = UX_MAXBLOCKS;
	buf->f_bfree = usb->s_nbfree;
	buf->f_bavail = usb->s_nbfree;
	buf->f_files = UX_MAXFILES;
	buf->f_ffree = usb->s_nifree;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);

	buf->f_namelen = UX_NAMELEN;
	return 0;
}

static void uxfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(uxfs_inode_cachep, UXFS_I(inode));
}

static void uxfs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, uxfs_i_callback);
}

static void init_once(void *foo)
{
	struct uxfs_inode_info *ui = (struct uxfs_inode_info *) foo;

	inode_init_once(&ui->vfs_inode);
}

static int __init init_inodecache(void)
{
	uxfs_inode_cachep = kmem_cache_create("uxfs_inode_cache",
					     sizeof(struct uxfs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once);
	if (uxfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

struct super_operations uxfs_sops = {
	.alloc_inode    = ux_alloc_inode,
	.destroy_inode  = uxfs_destroy_inode,
	.write_inode    = ux_write_inode,
	.evict_inode    = ux_evict_inode,
	.put_super      = ux_put_super,
	.statfs         = ux_statfs
};

static int ux_fill_super(struct super_block *s, void *data, int silent)
{
	struct ux_superblock *usb;
	struct ux_fs* fs;
	struct buffer_head *bh;
	struct inode* inode;

	int ret = -EINVAL;

	fs = (struct ux_fs*)kmalloc(sizeof(struct ux_fs), GFP_KERNEL);
	if(!fs)
		return -ENOMEM;

	s->s_fs_info = fs;

	if(!sb_set_blocksize(s, UX_BSIZE))
		goto out;

	bh = sb_bread(s, 0);
	if(!bh){
		goto out;
	}

	usb = (struct ux_superblock*)bh->b_data;
	if(usb->s_magic != UX_MAGIC){
		printk("unable to find ux filesystem\n");
		goto out;
	}

	if(usb->s_mode == UX_FSDIRTY){
		printk("filesystem is not clean, please run fsck\n");
	}

	/*
	mark the super block as dirty and wirte back to disk	
	*/

	fs->u_sb = usb;
	fs->u_sbh = bh;

	s->s_magic = UX_MAGIC;
	s->s_op = &uxfs_sops;

	printk("try to get an inode with iget_locked\n");
	inode = iget_locked(s, UX_ROOT_NO);
	if(!inode){
		goto out;
	}

	printk("got inode\n");
	ux_read_inode(inode);
	
	s->s_root = d_make_root(inode);
	if(!s->s_root){
		iput(inode);
		goto out;
	}

	printk("s_root = %p\n", s->s_root);
	unlock_new_inode(inode);
	return 0;
out:
	return ret;
}

static struct dentry *uxfs_domount(struct file_system_type *fs_type,int flags, const char *dev_name,void *data)
{
	printk("uxfs do mount: '%s'\n", dev_name);
	
	return	mount_bdev(fs_type, flags, dev_name, data, ux_fill_super);
}

static void destroy_inodecache(void)
{
	rcu_barrier();
	kmem_cache_destroy(uxfs_inode_cachep);
}

static struct file_system_type uxfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "uxfs",
	.mount = uxfs_domount,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("uxfs");

static int __init init_uxfs_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&uxfs_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	printk("error in init inodecache\n");
	return err;
}

static void __exit exit_uxfs_fs(void)
{
	printk("uxfs_exit!\n");
	unregister_filesystem(&uxfs_fs_type);
	destroy_inodecache();
}

module_init(init_uxfs_fs);
module_exit(exit_uxfs_fs);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("This is uxfs module");
MODULE_VERSION("Ver 0.1");
