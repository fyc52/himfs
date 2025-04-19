/*
 *   fs/samplefs/file.c
 *
 *   Copyright (C) International Business Machines  Corp., 2006
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Sample File System
 *
 *   Primitive example to show how to create a Linux filesystem module
 *
 *   File struct (file instance) related functions
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/mpage.h>
#ifndef _TEST_H_
#define _TEST_H_
#include "himfs_d.h"
#include "hash.h"
#endif

int himfs_get_block_prep(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh_result, int create)
{
	int ret = 0;
	lba_t ino = inode->i_ino;
 	lba_t lba = (DATA_REGIN_START_LBA + (ino << 9)) + iblock;
	bool new = false, boundary = false;
	
	/* todo: if pblk is a new block or update */
	if((iblock << BLOCK_SIZE_BITS) > inode->i_size)
	{
		new = true;
	}
	if(iblock > FILE_MAX_SIZE) 
	{
		boundary = true;
	}

	map_bh(bh_result, inode->i_sb, lba);//核心
	if (new)
	{
		set_buffer_new(bh_result);
	}

	if (boundary)
	{
		set_buffer_boundary(bh_result);
	}
	
	return ret;
}

static int himfs_write_begin(struct file *file, struct address_space *mapping,
                 loff_t pos, unsigned len, unsigned flags,
                 struct page **pagep, void **fsdata)
{	
	int ret;
	//("write begin\n");
	ret = block_write_begin(mapping, pos, len, flags, pagep, himfs_get_block_prep);
	struct inode *inode = mapping->host;
	struct himfs_inode_info *fi = HIMFS_I(inode);

	/* update himfs_inode size */
	loff_t end = pos + len;
	if (end >= mapping->host->i_size) {
		inode->i_size = end;
	}
	
	return ret;
}

static sector_t himfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, himfs_get_block_prep);
}

static int himfs_writepage(struct page *page, struct writeback_control *wbc)
{
	//printk(KERN_INFO "writepage\n");
	// dump_stack();
	return block_write_full_page(page, himfs_get_block_prep, wbc);
}

static int himfs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	//printk(KERN_INFO "writepages\n");
	// dump_stack();
	return mpage_writepages(mapping, wbc, himfs_get_block_prep);
}


static int himfs_readpage(struct file *file, struct page *page)
{
	//printk(KERN_INFO "readpage\n");
	return mpage_readpage(page, himfs_get_block_prep);
}

static int
himfs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	//printk(KERN_INFO "readpages\n");
	return mpage_readpages(mapping, pages, nr_pages, himfs_get_block_prep);
}

static ssize_t
himfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	//printk(KERN_INFO "himfs_direct_IO\n");
	return blockdev_direct_IO(iocb, inode, iter, himfs_get_block_prep);
}


int himfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	//printk(KERN_INFO "himfs file fsync");
	return  generic_file_fsync(file, start, end, datasync);
}

struct address_space_operations himfs_aops = {// page cache访问接口,未自定义的接口会调用vfs的generic方法
	.readpages	     = himfs_readpages,
	.readpage	     = himfs_readpage,
	.write_begin	 = himfs_write_begin,
	.write_end	     = generic_write_end,
	.bmap            = himfs_bmap,
	.set_page_dirty	 = __set_page_dirty_nobuffers,
	.writepages      = himfs_writepages,
	.writepage       = himfs_writepage,
	.direct_IO       = himfs_direct_IO,
};

static unsigned long himfs_mmu_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}
struct file_operations himfs_file_file_ops = {
	.read_iter		= generic_file_read_iter,
	.write_iter		= generic_file_write_iter,
	.mmap           = generic_file_mmap,
	.fsync			= himfs_fsync,
	.llseek         = generic_file_llseek,
};

static int himfs_readdir(struct file *file, struct dir_context *ctx)
{
	//printk(KERN_INFO "himfs read dir");
	loff_t pos;/*文件的偏移*/
	struct inode *ino = file_inode(file);
	struct super_block *sb = ino->i_sb;
	struct himfs_ino himfs_ino;
	struct himfs_sb_info *himfs_sb = sb->s_fs_info;

	return 0;
}

struct file_operations himfs_dir_operations = {
	.read			= generic_read_dir,
	.iterate		= himfs_readdir,//ls
	.iterate_shared = himfs_readdir,
	//.fsync			= himfs_fsync,
	//.release		= lightfs_dir_release,
};