
#include <linux/types.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/version.h>
#include <linux/nls.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/backing-dev.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/uaccess.h>
#include <linux/namei.h>
#include <linux/buffer_head.h>
#include <linux/uidgid.h>
#include <linux/atomic.h>

#ifndef _TEST_H_
#define _TEST_H_
#include "himfs_d.h"
#include "hash.h"
#endif

static int himfs_super_statfs(struct dentry *d, struct kstatfs *buf)
{
	return 0;
}

static void himfs_put_super(struct super_block *sb)
{
	struct himfs_sb_info *himfs_sb;

	himfs_sb = HIMFS_SB(sb);
	if (himfs_sb == NULL)
	{
		/* Empty superblock info passed to unmount */
		return;
	}

	/* FS-FILLIN your fs specific umount logic here */
	kfree(himfs_sb);
	return;
}

static void himfs_dirty_inode(struct inode *inode, int flags)
{
	struct buffer_head *bh;
	struct super_block *sb = inode->i_sb;
	struct himfs_inode *him_inode;
	lba_t lba;
	int idx;
	struct block_device *bdev = sb->s_bdev;
	struct inode *bdev_inode = bdev->bd_inode;
	struct himfs_meta_block *meta_block;

	if (bdev_inode == NULL) 
	{
		printk("bdev_inode error\n");
		return ;
	}

	//printk(KERN_INFO "sb->s_bdev = %d, fs type = %s, pblk = %lld\n", inode->i_sb->s_dev, sb->s_type->name, pblk);
	lba = inode->i_ino >> 3;
	bh = sb_bread(sb, lba);
	
 	if (unlikely(!bh))
	{
		printk(KERN_ERR "allocate bh for himfs_inode fail");
		return;
	}	

	idx = inode->i_ino & 0x7;
	him_inode = &(meta_block->himfs_inode[idx]);
    atomic64_t *atomic_ptr = (atomic64_t *)&inode->i_size;
    him_inode->i_size = (uint32_t)atomic64_read(atomic_ptr);
	atomic_ptr = (atomic64_t *)&inode->i_mtime;
	him_inode->i_mtime = (uint32_t)atomic64_read(atomic_ptr);
	atomic_ptr = (atomic64_t *)&inode->i_ctime;
	him_inode->i_ctime = (uint32_t)atomic64_read(atomic_ptr);

	set_buffer_uptodate(bh);//表示可以回写
	mark_buffer_dirty(bh);
	brelse(bh); //put_bh, 对应getblk
}

static void himfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	struct himfs_inode_info *fi = HIMFS_I(inode);
	// down_interruptible(&(fi->filename_sem));
	kmem_cache_free(himfs_inode_cachep, fi);
	// up(&(fi->filename_sem));
	//printk("inode->i_ino:%lld\n", inode->i_ino);	
}

static void himfs_destroy_inode(struct inode *inode)
{
	struct himfs_inode_info *fi = HIMFS_I(inode);
	call_rcu(&inode->i_rcu, himfs_i_callback);
}

static struct inode *himfs_alloc_inode(struct super_block *sb)
{
	struct himfs_inode_info *fi;
	fi = kmem_cache_alloc(himfs_inode_cachep, GFP_KERNEL);
	if (!fi)
		return NULL;
	atomic64_set(&fi->vfs_inode.i_version, 1);

	return &fi->vfs_inode;
}

static void himfs_free_in_core_inode(struct inode *inode)
{
	struct himfs_inode_info *fi = HIMFS_I(inode);
	// down_interruptible(&(fi->filename_sem));
	printk("inode->i_ino:%lld\n", inode->i_ino);
	kmem_cache_free(himfs_inode_cachep, fi);
// 	up(&(fi->filename_sem));
}

struct super_operations himfs_super_ops = {
	.statfs = himfs_super_statfs,
	.drop_inode = generic_delete_inode, /* VFS提供的通用函数，会判断是否定义具体文件系统的超级块操作函数delete_inode，若定义的就调用具体的inode删除函数(如ext3_delete_inode )，否则调用truncate_inode_pages和clear_inode函数(在具体文件系统的delete_inode函数中也必须调用这两个函数)。 */
	.put_super = himfs_put_super,
	.dirty_inode = himfs_dirty_inode,
	.alloc_inode = himfs_alloc_inode,
	//.free_inode	= himfs_free_in_core_inode,
	.destroy_inode	= himfs_destroy_inode,
};

struct inode *himfs_iget(struct super_block *sb, int mode, dev_t dev)
{
	struct himfs_inode_info *hii;
	struct buffer_head *bh = NULL;
	// struct himfs_inode *raw_inode;
	struct inode *inode;
	struct himfs_ino himfs_ino;
	struct himfs_meta_block *meta_block;
	struct himfs_inode *him_inode;
	// sector_t pblk;
	
	unsigned long root_ino = HIMFS_ROOT_INO;
	inode = iget_locked(sb, root_ino);
	
	if (inode)
	{
		himfs_ino.raw_ino = root_ino;
		hii = HIMFS_I(inode);
		inode->i_sb = sb;
		inode->i_mode = mode;													//访问权限,https://zhuanlan.zhihu.com/p/78724124
		inode->i_uid = current_fsuid();											/* Low 16 bits of Owner Uid */
		inode->i_gid = current_fsgid();											/* Low 16 bits of Group Id */
		inode->i_size = 0;													//文件的大小（byte）
		// 将时间戳赋值给相应的变量
		struct timespec64 cur_time = current_time(inode);
		inode->i_mtime = inode->i_ctime = cur_time;
		hii->i_crtime = (uint64_t)cur_time.tv_sec;
		//printk(KERN_INFO "about to set inode ops\n");
		inode->i_mapping->a_ops = &himfs_aops; // page cache操作
		// inode->i_mapping->backing_dev_info = &himfs_backing_dev_info;
		switch (mode & S_IFMT)
		{ /* type of file ，S_IFMT是文件类型掩码,用来取mode的0--3位,https://blog.csdn.net/wang93IT/article/details/72832775*/
		default:
			init_special_inode(inode, mode, dev); //为字符设备或者块设备文件创建一个Inode（在文件系统层）.
			break;
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
		}
	}
	// unlock_buffer(bh);

	bh = sb_bread(sb, root_ino >> HASH_SLOT_BITS);
	if (unlikely(!bh))
	{
		printk(KERN_ERR "allocate bh for himfs_inode fail");
		return 0;
	}

	meta_block = (struct himfs_meta_block*)bh->b_data;

	him_inode = &(meta_block->himfs_inode[0]);
	him_inode->i_mode = mode;
    him_inode->i_ino = himfs_ino.raw_ino;	 
    him_inode->i_uid = (uint16_t)__kuid_val(inode->i_uid);
    him_inode->i_gid = (uint16_t)__kgid_val(inode->i_gid);
    him_inode->i_size = 0;
    him_inode->i_ctime = inode->i_ctime.tv_sec;
    him_inode->i_mtime = inode->i_mtime.tv_sec;
    him_inode->i_crtime = hii->i_crtime;
    him_inode->i_detime = 0;
	him_inode->filename.name_len = strlen("/");
	strncpy(him_inode->filename.name, "/", strlen("/"));
    him_inode->i_pid = 0;

	set_bit(0, meta_block->slot_bitmap);

	set_buffer_uptodate(bh);//表示可以回写
	mark_buffer_dirty(bh);
	brelse(bh);

	return inode;
}

struct inode *himfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	struct inode *inode;
	inode = new_inode(sb); // https://blog.csdn.net/weixin_43836778/article/details/90236819
	struct himfs_inode_info *hii;

	if (inode)
	{
		hii = HIMFS_I(inode);
		inode->i_sb = sb;
		inode->i_mode = mode;													//访问权限,https://zhuanlan.zhihu.com/p/78724124
		inode->i_uid = current_fsuid();											/* Low 16 bits of Owner Uid */
		inode->i_gid = current_fsgid();											/* Low 16 bits of Group Id */
		inode->i_size = 0;													//文件的大小（byte）
		// 将时间戳赋值给相应的变量
		struct timespec64 cur_time = current_time(inode);
		inode->i_mtime = inode->i_ctime = cur_time;
		hii->i_crtime = (uint64_t)cur_time.tv_sec;
		//printk(KERN_INFO "about to set inode ops\n");
		inode->i_mapping->a_ops = &himfs_aops; // page cache操作
		// inode->i_mapping->backing_dev_info = &himfs_backing_dev_info;
		switch (mode & S_IFMT)
		{ /* type of file ，S_IFMT是文件类型掩码,用来取mode的0--3位,https://blog.csdn.net/wang93IT/article/details/72832775*/
		default:
			init_special_inode(inode, mode, dev); //为字符设备或者块设备文件创建一个Inode（在文件系统层）.
			break;
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
		case S_IFLNK://symlink
			inode->i_op = &page_symlink_inode_operations;
			inode_nohighmem(inode);
			break;
		}
	}
	inode->i_state |= I_NEW;
	
	return inode;
}

int sb_set_blocksize(struct super_block *sb, int size)
{
	if (sb_set_blocksize(sb, size))
		return 0;
	
	sb->s_blocksize = size;
	int bits = 0;
	while ((size & 1) == 0) 
	{
		size >>= 1;
		bits++;
	}
	sb->s_blocksize_bits = bits;
	return sb->s_blocksize;
}

static int himfs_fill_super(struct super_block *sb, void *data, int silent) // mount时被调用，会创建一个sb
{
	struct inode *inode;
	int blocksize = BLOCK_SIZE;
	struct himfs_sb_info *himfs_sb;
	unsigned long logic_sb_block = 0;
	loff_t dir_size;

	struct buffer_head *bh;
	blocksize = sb_min_blocksize(sb, BLOCK_SIZE);
	if (!(bh = sb_bread(sb, logic_sb_block))) 
	{
		printk( KERN_ERR, "error: unable to read superblock");
	}
	
	himfs_sb = kzalloc(sizeof(struct himfs_sb_info), GFP_NOIO);
	strcpy(himfs_sb->fs_name, sb->s_type->name);

	printk(KERN_INFO "himfs_sb->name: %s\n", himfs_sb->fs_name);
	sb->s_maxbytes = MAX_LFS_FILESIZE;					 /*文件大小上限*/
	sb->s_blocksize = HIMFS_BSTORE_BLOCKSIZE;			 //以字节为单位的块大小
	sb->s_blocksize_bits = HIMFS_BSTORE_BLOCKSIZE_BITS; //以位为单位的块大小
	sb->s_magic = HIMFS_MAGIC;							 //可能是用来内存分配的地址
	sb->s_op = &himfs_super_ops;						 // sb操作=
	sb->s_time_gran = 1;								 /* 时间戳的粒度（单位为纳秒) */
	printk(KERN_INFO "himfs: fill super\n");

	inode = himfs_iget(sb, S_IFDIR | 0755, 0); //分配根目录的inode,增加引用计数，对应iput;S_IFDIR表示是一个目录,后面0755是权限位:https://zhuanlan.zhihu.com/p/48529974
	if (!inode)
		return -ENOMEM;

	inode->i_ino = HIMFS_ROOT_INO;//为根inode分配ino#，不能为0
	printk(KERN_INFO "himfs: root inode = %lx\n", inode->i_ino);
	
	dir_size = i_size_read(inode);

	printk("himfs_inode_page size:%lu\n", sizeof(struct himfs_meta_block));
	sb->s_fs_info = himfs_sb;
	//himfs_sb->s_sb_block = sb_block;
	//kzalloc(sizeof(struct himfs_sb_info), GFP_KERNEL); // kzalloc=kalloc+memset（0），GFP_KERNEL是内存分配标志
	printk(KERN_INFO "himfs: sb->s_fs_info init ok\n");
	himfs_sb = HIMFS_SB(sb);
	if (!himfs_sb)
	{
		iput(inode);
		return -ENOMEM;
	}

	sb->s_root = d_make_root(inode); //用来为fs的根目录（并不一定是系统全局文件系统的根“／”）分配dentry对象。它以根目录的inode对象指针为参数。函数中会将d_parent指向自身，注意，这是判断一个fs的根目录的唯一准则
	if (!sb->s_root)
	{ //分配结果检测，如果失败
		printk(KERN_INFO "root node create failed\n");
		iput(inode);
		//kfree(himfs_sb->cuckoo);
		//himfs_sb->cuckoo=NULL;
		kfree(himfs_sb);
		return -ENOMEM;
	}

	mark_buffer_dirty(bh);
	unlock_new_inode(inode);
	brelse(bh);
	/* FS-FILLIN your filesystem specific mount logic/checks here */
	return 0;
}
/*
 * mount himfs, call kernel util mount_bdev
 * actual work of himfs is done in himfs_fill_super
 */
static struct dentry *himfs_mount(struct file_system_type *fs_type,
								   int flags, const char *dev_name, void *data)
{
	// return mount_nodev(fs_type, flags, data, himfs_fill_super);//内存文件系统，无实际设备,https://zhuanlan.zhihu.com/p/482045070
	printk(KERN_INFO "start mount of himfs\n");
	return mount_bdev(fs_type, flags, dev_name, data, himfs_fill_super);
}

static void himfs_kill_sb(struct super_block *sb)
{
	printk(KERN_INFO "kill_sb of himfs\n");
	//dir_exit(sb->s_fs_info);
	sync_filesystem(sb);
	kill_block_super(sb);
	printk(KERN_INFO "kill_sb of himfs OK\n");
}

static struct file_system_type himfs_fs_type = {
	//文件系统最基本的变量
	.owner = THIS_MODULE,
	.name = "himfs",
	.mount = himfs_mount,	   //创建sb,老版本get_sb
	.kill_sb = himfs_kill_sb, //删除sb
};

static void init_once(void *foo)
{
	struct himfs_inode_info *fi = (struct himfs_inode_info *) foo;
	inode_init_once(&fi->vfs_inode);
}

static int __init init_inodecache(void)
{
	himfs_inode_cachep = kmem_cache_create("himfs_inode_cache",
					     sizeof(struct himfs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT|SLAB_HWCACHE_ALIGN),
					     init_once);
	if (himfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(himfs_inode_cachep);
}

static int __init init_himfs_fs(void) //宏定义__init表示该函数旨在初始化期间使用，模块装载后就扔掉，释放内存
{
	int err;
	printk(KERN_INFO "init himfs\n");
	err = init_inodecache();
	if (err)
		return err;
	err = register_filesystem(&himfs_fs_type); //内核文件系统API,将himfs添加到内核文件系统链表
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
	return err;	
}

static void __exit exit_himfs_fs(void)
{
	unregister_filesystem(&himfs_fs_type);
	destroy_inodecache();
}

module_init(init_himfs_fs); //宏：模块加载, 调用init_himfs_fs
module_exit(exit_himfs_fs);
MODULE_LICENSE("GPL");
