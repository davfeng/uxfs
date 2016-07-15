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
#define AUFS_MAGIC 0x64668735

static struct kmem_cache *uxfs_inode_cachep;

static struct inode* ux_alloc_inode(struct super_block *sb)
{
	struct uxfs_inode_info* ui;
	ui = kmem_cache_alloc(uxfs_inode_cachep, GFP_KERNEL);
	if (!ui)
		return NULL;
	return &ui->vfs_inode; 
}

int ux_find_entry(struct inode *dip, char *name)
{
	struct super_block *sb = dip->i_sb;
	struct uxfs_inode_info *ui = UXFS_I(dip);
	struct buffer_head *bh = NULL;
	struct ux_dirent   *dirent;
	int    i, blk = 0;

	for (blk=0 ; blk < dip->i_blocks ; blk++) {
		bh = sb_bread(sb, ui->i_addr[blk]);
		dirent = (struct ux_dirent *)bh->b_data;
			for (i=0 ; i < UX_DIRS_PER_BLOCK ; i++) {
				if (strcmp(dirent->d_name, name) == 0) {
					brelse(bh);
					return dirent->d_ino;
				}
				dirent++;
		}
	}

	if (bh)
		brelse(bh);
	return 0;
}

void ux_read_inode(struct inode *inode)
{
	struct buffer_head	  *bh;
	struct ux_inode		  *di;
	unsigned long		  ino = inode->i_ino;
	int			  block;

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

	di = (struct ux_inode *)(bh->b_data);
	inode->i_mode = di->i_mode;
	if (di->i_mode & S_IFDIR) {
		inode->i_mode |= S_IFDIR;
		inode->i_op = &ux_dir_inops;
		inode->i_fop = &ux_dir_operations;
	} else if (di->i_mode & S_IFREG) {
		inode->i_mode |= S_IFREG;
		inode->i_op = &ux_file_inops;
		inode->i_fop = &ux_file_operations;
		inode->i_mapping->a_ops = &ux_aops;
	}
	i_uid_write(inode, le32_to_cpu(di->i_uid));
	i_gid_write(inode, le32_to_cpu(di->i_gid));
	set_nlink(inode, di->i_nlink);
	inode->i_size = di->i_size;
	inode->i_blocks = di->i_blocks;
	inode->i_blkbits = 9;
	inode->i_atime.tv_sec = __le32_to_cpu(di->i_atime);
	inode->i_mtime.tv_sec = __le32_to_cpu(di->i_mtime);
	inode->i_ctime.tv_sec = __le32_to_cpu(di->i_ctime);
	brelse(bh);
}

static int ux_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	unsigned long ino = inode->i_ino;
	struct ux_inode *uip = (struct ux_inode*)inode->i_private;
	struct buffer_head *bh;
	__u32 blk;

	printk("uxfs: ux_write_inode\n");
	if(ino < UX_ROOT_NO || ino > UX_MAXFILES){
		printk("uxfs: Bad inode number %lu\n", ino);
		return -1;
	}
	
	blk = UX_INODE_BLOCK + ino;
	bh = sb_bread(inode->i_sb, blk);
	uip->i_mode = inode->i_mode;
	uip->i_nlink = inode->i_nlink;
	//uip->i_atime = inode->i_atime;
	//uip->i_mtime = inode->i_mtime;
	//uip->i_ctime = inode->i_ctime;
	uip->i_uid = cpu_to_le32(i_uid_read(inode));
	uip->i_gid = cpu_to_le32(i_gid_read(inode));
	uip->i_size = cpu_to_le32(inode->i_size);
	memcpy(bh->b_data, uip, sizeof(struct ux_inode));
	mark_buffer_dirty(bh);
	brelse(bh);
	return 0;
}

void ux_put_super(struct super_block* s)
{
	struct ux_fs *fs = (struct ux_fs*)s->s_fs_info;
	struct buffer_head *bh = fs->u_sbh;
	kfree(fs);
	brelse(bh); 
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
	.kill_sb = kill_litter_super,
	.fs_flags = FS_REQUIRES_DEV,
};

static int __init init_uxfs_fs(void)
{	
	int err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&uxfs_fs_type);
	if (err)
		goto out;
out:
	destroy_inodecache();
out1:
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
