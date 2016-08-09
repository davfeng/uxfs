#include <linux/sched.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "ux_fs.h"

static struct buffer_head* ux_find_entry(struct inode *dir, const char *name, struct ux_dirent **res_dir)
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

/*
 * Add "name" to the directory dir
 */

int ux_add_entry(struct inode *dir, const char *name, int namelen, int inum)
{
	struct uxfs_inode_info *ui = UXFS_I(dir);
	struct buffer_head    *bh;
	struct super_block    *sb = dir->i_sb;
	struct ux_dirent      *dirent;
	__u32		      blk = 0;
	int		      i, j, pos;

	printk("dir->i_size: %d\n", (int)dir->i_size);
	printk("dir->i_blocks: %d\n", (int)dir->i_blocks);

	for (blk=0 ; blk < dir->i_blocks ; blk++) {
		printk("ui->i_addr[%d] = %d\n", blk, (int)ui->i_addr[blk]);
		bh = sb_bread(sb, ui->i_addr[blk]);
		if (!bh)
			return -EIO;
		dirent = (struct ux_dirent *)bh->b_data;
		for (i=0 ; i < UX_DIRS_PER_BLOCK ; i++) {
			if (dirent->d_ino != 0) {
				dirent++;
				continue;
			} else {
				dirent->d_ino = inum;
				for(j = 0; j < UX_NAMELEN; j++)
					dirent->d_name[j] = ((j < namelen) ? name[j] : 0);
				dir->i_mtime = CURRENT_TIME_SEC;
				mark_inode_dirty(dir);
				mark_buffer_dirty_inode(bh, dir);
				brelse(bh);
				return 0;
			}
		}
		brelse(bh);
	}

	/*
	 * We didn't find an empty slot so need to allocate 
	 * a new block if there's space in the inode.
	 */

	if (dir->i_blocks < UX_DIRECT_BLOCKS) {
		pos = dir->i_blocks;
		blk = ux_block_alloc(sb);
		dir->i_blocks++;
		dir->i_size += UX_BSIZE;
		ui->i_addr[pos] = blk;
		bh = sb_bread(sb, blk);
		memset(bh->b_data, 0, UX_BSIZE);
		mark_inode_dirty(dir);
		dirent = (struct ux_dirent *)bh->b_data;
		dirent->d_ino = inum;
		strcpy(dirent->d_name, name);
		mark_buffer_dirty(bh);
		brelse(bh);
	}

	return 0;
}

int ux_readdir(struct file *filp, struct dir_context *ctx)
{
	struct inode	      *dir = file_inode(filp);
	struct uxfs_inode_info *ui = UXFS_I(dir);
	struct ux_dirent      *udir;
	struct buffer_head    *bh;
	__u32	blk;
	unsigned int	offset;

	printk("dir = %p, ui = %p \n", dir, ui);
	printk("ux_readdir dir->i_ino = %08lx, ctx->pos = %08lx, ui->i_blocks = %u\n", dir->i_ino, (unsigned long)ctx->pos, ui->i_blocks);
	if (ctx->pos & (UX_DIRENT_SIZE - 1)){
		printk("Bad f_pos=%08lx for %s:%08lx\n", (unsigned long)ctx->pos, dir->i_sb->s_id, dir->i_ino);
		return -EINVAL;
	}
	
	while (ctx->pos < dir->i_size) {
		blk = ctx->pos >> UX_BSIZE_BITS;
		blk = ui->i_addr[blk];
		bh = sb_bread(dir->i_sb, blk);
		offset = ctx->pos & (UX_BSIZE - 1);

		do {
			udir = (struct ux_dirent *)(bh->b_data + offset);
			printk("ux_readdir udir->d_ino = %u , udir->d_name = %s\n", udir->d_ino, udir->d_name);
			if (udir->d_ino){
				int size = strnlen(udir->d_name, UX_NAMELEN);
				if(!dir_emit(ctx, udir->d_name, size, (u64)udir->d_ino, DT_UNKNOWN)){
					brelse(bh);
					return 0;
				}
			}
			ctx->pos += sizeof(struct ux_dirent);
			offset += sizeof(struct ux_dirent);
		} while ((offset < UX_BSIZE) && (ctx->pos < dir->i_size));
		brelse(bh);
	}
	return 0;	 
}

struct file_operations ux_dir_operations = {
	.read		= generic_read_dir,
	.iterate    = ux_readdir,
	.fsync      = generic_file_fsync,
    .llseek     = generic_file_llseek,
};

/*
 * When we reach this point, ux_lookup() has already been called
 * to create a negative entry in the dcache. Thus, we need to
 * allocate a new inode on disk and associate it with the dentry.
 */

static int ux_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	struct super_block		*sb = dir->i_sb;
	struct inode			*inode;
	ino_t					inum = 0;
	struct buffer_head		*bh = NULL;
	struct ux_dirent		*de = NULL;
		
	/*
	 * See if the entry exists. If not, create a new 
	 * disk inode, and incore inode. The add the new 
	 * entry to the directory.
	 */ 

	printk("ux_create \n");
	bh = ux_find_entry(dir, (char *)dentry->d_name.name, &de);
	if (bh) {
		brelse(bh);
		return -EEXIST;
	}
	
	inode = new_inode(sb);
	if (!inode) {
		return -ENOSPC;
	}

	inum = ux_ialloc(sb);
	if (!inum) {
		iput(inode);
		return -ENOSPC;
	}

	printk("ux_create : inum = %d\n", (int)inum);
	
	/*
	 * Increment the parent link count and intialize the inode.
	 */

	inode_init_owner(inode, dir, mode);
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	inode->i_blkbits = UX_BSIZE_BITS;
	inode->i_blocks = 0;
	inode->i_op = &ux_file_inops;
	inode->i_fop = &ux_file_operations;
	inode->i_mapping->a_ops = &ux_aops;
	inode->i_mode = mode;
	inode->i_ino = inum;
	insert_inode_hash(inode); 
	mark_inode_dirty(inode);

	ux_add_entry(dir, (char *)dentry->d_name.name, dentry->d_name.len, inum);

	d_instantiate(dentry, inode);
	mark_buffer_dirty(((struct ux_fs *)sb->s_fs_info)->u_sbh);
	return 0;
}

/*
 * Lookup the specified file. A call is made to iget() to
 * bring the inode into core.
 */

static struct dentry* ux_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	struct inode		*inode = NULL;
	struct ux_inode		*ui;
	struct buffer_head	*bh, *bh1;
	struct ux_dirent	*de;
	int					inum;

	if (dentry->d_name.len > UX_NAMELEN) {
		return ERR_PTR(-ENAMETOOLONG);
	}

	printk("ux_lookup dentry->d_name.name=%s, dentry->d_name.len=%u\n", dentry->d_name.name, dentry->d_name.len);
	bh1 = ux_find_entry(dir, (char *)dentry->d_name.name, &de);
	if (bh1 && (inum = de->d_ino)) {
		printk("ux_lookup inum = %d\n", inum);
		inode = iget_locked(dir->i_sb, inum);
		if (!inode) {
			return ERR_PTR(-EACCES);
		}
		bh = sb_bread(inode->i_sb, inum + 8);
		if (!bh) {
			printk("Unable to read inode %s:%08lx\n", inode->i_sb->s_id, inum);
			return ERR_CAST(ERR_PTR(-EIO));
		}

		ui = (struct ux_inode*)bh->b_data;
		printk(" ui->i_mode = %08lx\n", ui->i_mode);
		inode->i_mode = ui->i_mode;
		if (ui->i_mode & S_IFDIR){
			printk("folder!!!\n");
			inode->i_op = &ux_dir_inops;
			inode->i_fop = &ux_dir_operations;
		}else if (ui->i_mode & S_IFREG){
			printk("regular file!!!\n");
			inode->i_op = &ux_file_inops;
			inode->i_fop = &ux_file_operations;
			inode->i_mapping->a_ops = &ux_aops;
		}

		i_uid_write(inode, ui->i_uid);	
		i_gid_write(inode, ui->i_gid);	
		set_nlink(inode, ui->i_nlink);

		memcpy(UXFS_I(inode)->i_addr, ui->i_addr, sizeof(ui->i_addr));

		inode->i_size = ui->i_size;
		inode->i_blocks = ui->i_blocks;
		inode->i_atime.tv_sec = ui->i_atime;
		inode->i_mtime.tv_sec = ui->i_mtime;
		inode->i_ctime.tv_sec = ui->i_ctime;
		inode->i_atime.tv_nsec = 0;
		inode->i_mtime.tv_nsec = 0;
		inode->i_ctime.tv_nsec = 0;
		brelse(bh);
		unlock_new_inode(inode);
	}
	d_add(dentry, inode);
	return NULL;
}

/*
 * Called in response to an ln command/syscall.
 */

static int ux_link(struct dentry *old, struct inode *dir, struct dentry *new)
{
	struct inode	   *inode = d_inode(old);
	int		   error;

	printk("ux_link\n");
	/*
	 * Add the new file (new) to its parent directory (dir)
	 */

	error = ux_add_entry(dir, new->d_name.name, new->d_name.len,inode->i_ino);

	/*
	 * Increment the link count of the target inode
	 */

	inode->i_ctime = CURRENT_TIME_SEC;
	inode_inc_link_count(inode);
	ihold(inode);
	d_instantiate(new, inode);
	return 0;
}

/*
 * Called to remove a file (decrement its link count)
 */

static int ux_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	struct uxfs_inode_info	*ui = UXFS_I(dir);
	struct buffer_head	*bh;
	struct super_block	*sb = dir->i_sb;
	struct ux_dirent	*dirent;
	__u32			blk = 0;
	int			i;

	printk("ux_unlink, inode->i_nlink = %d inode->i_count = %d\n", inode->i_nlink, inode->i_count);
	while (blk < dir->i_blocks) {
		bh = sb_bread(sb, ui->i_addr[blk]);
		blk++;
		dirent = (struct ux_dirent *)bh->b_data;
		for (i=0 ; i < UX_DIRS_PER_BLOCK ; i++) {
			if (strcmp(dirent->d_name, dentry->d_name.name) != 0) {
				dirent++;
				continue;
			} else {
				dirent->d_ino = 0;
				dirent->d_name[0] = '\0';
				mark_buffer_dirty(bh);
				dir->i_ctime = dir->i_mtime = CURRENT_TIME_SEC;
				mark_inode_dirty(dir);
				break;
			}
		}
		brelse(bh);
	}
	inode->i_ctime = dir->i_ctime;
	inode_dec_link_count(inode);
	return 0;
}

static int ux_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *old_inode, *new_inode;
	struct buffer_head *old_bh = NULL, *new_bh = NULL;
	struct ux_dirent *old_de, *new_de;
	int error = -ENOENT;

	old_inode = d_inode(old_dentry);
	if (S_ISDIR(old_inode->i_mode))
		return -EINVAL;

	old_bh = ux_find_entry(old_dir, old_dentry->d_name.name, &old_de);

	if (!old_bh || (old_de->d_ino != old_inode->i_ino))
		goto end_rename;

	error = -EPERM;
	new_inode = d_inode(new_dentry);
	new_bh = ux_find_entry(new_dir, new_dentry->d_name.name, &new_de);

	if(new_bh && !new_inode){
		brelse(new_bh);
		new_bh = NULL;
	}
	if (!new_bh) {
		error = ux_add_entry(new_dir, 
					new_dentry->d_name.name,
					new_dentry->d_name.len,
					old_inode->i_ino);
		if (error)
			goto end_rename;
	}
	old_de->d_ino = 0;
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(old_dir);
	if (new_inode) {
		new_inode->i_ctime = CURRENT_TIME_SEC;
		inode_dec_link_count(new_inode);
	}
	mark_buffer_dirty_inode(old_bh, old_dir);
	error = 0;

end_rename:
	brelse(old_bh);
	brelse(new_bh);
	return error;
}

struct inode_operations ux_dir_inops = {
	.create = ux_create,
	.lookup = ux_lookup,
	.link   = ux_link,
	.unlink = ux_unlink,
	.rename = ux_rename,
};


