#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/blk_types.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>

#ifndef _TEST_H_
#define _TEST_H_
#include "himfs_d.h"
#include "hash.h"
#endif

extern struct inode *himfs_get_inode(struct super_block *sb, int mode, dev_t dev);

#include <linux/atomic.h>
#include <linux/time.h>

static void update_dir(struct inode *inode, struct inode *dir, bool is_create)
{
    uint64_t atomic_cur;
    uint64_t atomic_update;
    bool success;
    struct himfs_inode_info *hii = HIMFS_I(inode);
    atomic_t *dir_size = (atomic_t *)&(dir->i_size);

    // 更新目录大小
    if (is_create)
	{
        atomic_inc(dir_size);
    } 
	else 
	{
        atomic_dec(dir_size);
    }

    // 设置更新时间
    atomic_update = is_create ? hii->i_crtime : hii->i_detime;

    // 更新 i_ctime
    do {
        atomic64_t *atomic_ptr = (atomic64_t *)&(dir->i_ctime.tv_sec);
        atomic_cur = atomic64_read(atomic_ptr);

        if (atomic_cur >= atomic_update) 
		{
            goto out;
        }

        // 使用原子比较并交换操作尝试更新时间戳
        success = atomic64_cmpxchg(atomic_ptr, atomic_cur, atomic_update) == atomic_cur;
    } while (!success);

    // 更新 i_mtime
    success = false;
    do {
        atomic64_t *atomic_ptr = (atomic64_t *)&(dir->i_mtime.tv_sec);
        atomic_cur = atomic64_read(atomic_ptr);

        if (atomic_cur >= atomic_update) 
		{
            goto out;
        }

        // 使用原子比较并交换操作尝试更新时间戳
        success = atomic64_cmpxchg(atomic_ptr, atomic_cur, atomic_update) == atomic_cur;
    } while (!success);

    // 标记 inode 为脏
    mark_inode_dirty(dir);
out:
    return;
}

//调用具体文件系统的lookup函数找到当前分量的inode，并将inode与传进来的dentry关联（通过d_splice_alias()->__d_add）
//dir:父目录的inode；
//dentry：本目录的dentry，需要关联到本目录的inode
static struct dentry *himfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)	
{
	//printk(KERN_INFO "himfs: lookup, name = %s\n", dentry->d_name.name);
	struct inode *inode;
	unsigned long ino = 0;
	struct buffer_head *bh;
	struct himfs_meta_block* meta_block;
	struct himfs_inode *him_inode;
	struct himfs_inode_info *hii = HIMFS_I(inode);
	struct himfs_sb_info *himfs_sb = dir->i_sb->s_fs_info;
	int idx;

	if (dentry->d_name.len > HIMFS_MAX_FILENAME_LEN)
	{
		goto out;
	}

	bh = hash_get(dir, dentry, &idx);
		
	/* 子目录树和子文件中均没找到，说明没有这个子文件/目录 */
	if(bh == NULL) 
	{ 
		inode = NULL;
		//printk(KERN_INFO "inode is NULL\n");
		goto out;
	}

	meta_block = (struct himfs_meta_block*)bh->b_data;
	him_inode = &(meta_block->himfs_inode[idx]);

	inode = iget_locked(dir->i_sb, him_inode->i_ino);
	if (!inode)
	{
		printk("iget_locked err\n");
		return ERR_PTR(-ENOMEM);
	}

	if (!(inode->i_state & I_NEW)) {
		/* 在内存中有最新的inode，直接结束 */
		//printk(KERN_INFO "himfs: new inode OK\n");
		goto out;
	}
	
	// 用盘内inode赋值inode操作
	inode->i_sb = dir->i_sb;
	inode->i_ino = him_inode->i_ino;		
	inode->i_mode = him_inode->i_mode;													//访问权限,https://zhuanlan.zhihu.com/p/78724124
	inode->i_uid = make_kuid(&init_user_ns, him_inode->i_uid);								/* Low 16 bits of Owner Uid */
	inode->i_gid = make_kgid(&init_user_ns, him_inode->i_gid);													/* Low 16 bits of Group Id */
	inode->i_size = him_inode->i_size;												//文件的大小（byte）
	hii->i_crtime = him_inode->i_crtime;		
	struct timespec64 mtime, ctime;
	mtime.tv_sec = him_inode->i_mtime;
	mtime.tv_nsec = 0;  // 纳秒部分设为 0
	ctime.tv_sec = him_inode->i_ctime;
	ctime.tv_nsec = 0;  // 纳秒部分设为 0

	inode->i_mtime = mtime;
	inode->i_ctime = ctime;	

	//printk(KERN_INFO "about to set inode ops\n");
	inode->i_mapping->a_ops = &himfs_aops; // page cache操作
	// inode->i_mapping->backing_dev_info = &himfs_backing_dev_info;
	switch (inode->i_mode & S_IFMT)
	{ /* type of file ，S_IFMT是文件类型掩码,用来取mode的0--3位,https://blog.csdn.net/wang93IT/article/details/72832775*/
	case S_IFREG: /* regular 普通文件*/
		//printk(KERN_INFO "file inode\n");
		inode->i_op = &himfs_file_inode_ops;
		inode->i_fop = &himfs_file_file_ops;
		inode->i_mapping->a_ops = &himfs_aops;
		break;
	case S_IFDIR: /* directory 目录文件*/

		inode->i_op = &himfs_dir_inode_ops;
		inode->i_fop = &himfs_dir_operations;
		inode->i_mapping->a_ops = &himfs_aops;
		inc_nlink(inode); // i_nlink是文件硬链接数,目录是由至少2个dentry指向的：./和../，所以是2；这里只加1，外层再加1
		break;
	default:
		break;
	}

	inc_nlink(inode);		
	unlock_new_inode(inode);
	brelse(bh);
out:
	return d_splice_alias(inode, dentry);//将inode与dentry绑定
}

static int himfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode = himfs_get_inode(dir->i_sb, mode, dev); //分配VFS inode
	int error = -ENOSPC;

	if (dentry->d_name.len >= HIMFS_MAX_FILENAME_LEN) 
	{
		printk("file name len error\n");
		return error;
	}

	inode->i_ino = 0;
	inode->i_ino = hash_insert(inode, dir, dentry, mode);

	if (inode->i_ino != 0)
	{
		error = 0;
		insert_inode_locked(inode);//将inode添加到inode hash表中，并标记为I_NEW
		unlock_new_inode(inode);
		d_instantiate(dentry, inode);//将dentry和新创建的inode进行关联
		update_dir(inode, dir, true);
	}
	else
	{
		iput(inode);
	}

	return error;
}

static int himfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	int err;
	err = himfs_mknod(dir, dentry, mode | S_IFREG, 0);

	if(err != 0)
	{
		printk("Create failed!");
	}

	return err;
}


static int himfs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	int ret = himfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!ret)
		inc_nlink(dir);
	return ret;
}


static int himfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct inode_context ctx;
	int err = 0;

	ctx.is_delete = true;
	ctx.inode = inode;
	err = hash_update(dir, dentry, &ctx);
	if (err) 
	{
		printk("unlink failed\n");
	}

	update_dir(inode, dir, false);
	iput(inode);

	return -1;
}

static int himfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	// struct inode * pinode = d_inode(dentry->d_parent);
	int err = -ENOTEMPTY;
	struct himfs_sb_info *himfs_sb_i = HIMFS_SB(dir->i_sb);
	//sector_t meta_start, meta_size, data_start, data_size;
	
	if(!i_size_read(inode))
	{
		err = himfs_unlink(dir, dentry);
		if(!err)
		{
			inode->i_size = 0;
			inode_dec_link_count(inode);
			inode_dec_link_count(dir);
		}
		return 0;
	}
	return err;
}

static int himfs_rename(struct inode * old_dir, struct dentry * old_dentry,
			struct inode * new_dir,	struct dentry * new_dentry,
			unsigned int flags)
{
	struct inode * old_inode = d_inode(old_dentry);
	struct inode * new_inode = d_inode(new_dentry);
	int err = 0;

	return err;
}

struct inode_operations himfs_file_inode_ops = {
    .setattr	= simple_setattr,
	.getattr	= simple_getattr,
};

struct inode_operations himfs_dir_inode_ops = {
	.create         = himfs_create,
	.lookup         = himfs_lookup,
	.link			= simple_link,
	.unlink         = himfs_unlink,
	//.symlink		= himfs_symlik,
	.mkdir          = himfs_mkdir,
	.rmdir          = himfs_rmdir,
	.mknod          = himfs_mknod,	//该函数由系统调用mknod（）调用，创建特殊文件（设备文件、命名管道或套接字）。要创建的文件放在dir目录中，其目录项为dentry，关联的设备为rdev，初始权限由mode指定。
	.rename         = himfs_rename,
};


