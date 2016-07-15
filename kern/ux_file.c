#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "ux_fs.h"

struct file_operations ux_file_operations = {
	.llseek     = generic_file_llseek,
	.read_iter  = generic_file_read_iter,
	.write_iter = generic_file_write_iter,
	.mmap       = generic_file_mmap,
};

int ux_get_block(struct inode *inode, sector_t block, struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	struct uxfs_inode_info *ui = UXFS_I(inode);

	/*
	 * First check to see is the file can be extended.
	 */

	if (block >= UX_DIRECT_BLOCKS) {
		return -EFBIG;
	}

	if (!create) {
		if (block < inode->i_blocks) {
			map_bh(bh_result, inode->i_sb, ui->i_addr[block]);
		}
		return 0;
	}
	
	/*
	 * If we're creating and , we must allocate a new block.
	 */

	if (inode->i_blocks > 0 && block < inode->i_blocks){
		map_bh(bh_result, inode->i_sb, ui->i_addr[block]);
		return 0;
	}

	if (block >= inode->i_blocks) {
		printk("uxfs: ux_get_block - Out of space\n");
		return -ENOSPC;
	}
	mark_inode_dirty(inode);
	return 0;
}

int ux_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ux_get_block, wbc);
}

int ux_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, ux_get_block);
}

static void ux_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size)
		truncate_pagecache(inode, inode->i_size);
}

int ux_write_begin(struct file *file, struct address_space *mapping, loff_t pos, unsigned len, unsigned flags, struct page **pagep, void **fsdata)
{
	int ret;
	ret = block_write_begin(mapping, pos, len, flags, pagep, ux_get_block);
	if (unlikely(ret))
		ux_write_failed(mapping, pos + len);
	return ret;
}

int ux_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, ux_get_block);
}

struct address_space_operations ux_aops = {
	.readpage	    = ux_readpage,
	.writepage	    = ux_writepage,
	.write_begin    = ux_write_begin,
	.write_end	    = generic_write_end,
	.bmap		    = ux_bmap,
};

struct inode_operations ux_file_inops = {
	link:		  ux_link,
	unlink:		  ux_unlink,
};
