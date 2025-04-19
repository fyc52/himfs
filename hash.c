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
#include <linux/vmalloc.h>
#ifndef _TEST_H_
#define _TEST_H_
#include "himfs_d.h"
#include "hash.h"
#endif

// BKDR Hash Function
unsigned int BKDRHash(char *str, int len)
{
	unsigned int seed = 10;
	unsigned int hash = 0;

	while (len > 0)
	{
		hash = hash * seed + *str - '0';
		str++;
		len--;
	}

	return (hash & 0x7FFFFFFF);
}

uint32_t murmurHash3(uint32_t key1, const char* key2, int len) 
{
    const uint8_t *data = (const uint8_t *)key2;
    const int nblocks = len / 4;

	uint32_t seed = 4397;
    uint32_t h1 = seed;

    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
	int i;

    // 处理每4字节块
    for (i = 0; i < nblocks; i++) {
        uint32_t k1 = *(uint32_t *)(data + i * 4);

        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> (32 - 15));
        k1 *= c2;

        h1 ^= k1;
        h1 = (h1 << 13) | (h1 >> (32 - 13));
        h1 = h1 * 5 + 0xe6546b64;
    }

    // 处理剩余的字节
    const uint8_t *tail = data + nblocks * 4;
    uint32_t k1 = 0;

    switch (len & 3) {
        case 3: k1 ^= tail[2] << 16;
        case 2: k1 ^= tail[1] << 8;
        case 1: k1 ^= tail[0];
                k1 *= c1;
                k1 = (k1 << 15) | (k1 >> (32 - 15));
                k1 *= c2;
                h1 ^= k1;
    }

    // 最终混合
    h1 ^= len;
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;

    // 将第一个uint32_t键值混入哈希值
    h1 ^= key1;

    return h1;
}

struct buffer_head* hash_get(struct inode *dir, struct dentry *dentry, int *idx)
{
	lba_t lba;
	struct buffer_head *buffer;
	struct himfs_meta_block *meta_block;
	struct himfs_inode *him_inode;

	lba = murmurHash3(dir->i_ino, dentry->d_name.name, dentry->d_name.len);
	lba %= META_REGIN_END_LBA;
	if (lba < META_REGIN_START_LBA) 
	{
		lba = META_REGIN_START_LBA;
	}

	buffer = sb_bread(dir->i_sb, lba);

	if (unlikely(!buffer))
	{
		printk(KERN_ERR "allocate bh for himfs_inode fail");
		return NULL;
	}

	meta_block = (struct himfs_meta_block*)buffer->b_data;

	for (*idx = 0; *idx < HASH_SLOT_NUM; ++(*idx))
	{
		if (test_bit(*idx, meta_block->slot_bitmap))
		{
			him_inode = &meta_block->himfs_inode[*idx];
			if (strcmp(him_inode->filename.name, dentry->d_name.name) == 0)
			{
				break;
			}
		}
	}

	if (*idx == HASH_SLOT_NUM)
	{
		return NULL;
	}

	return buffer;
}

unsigned int hash_insert(struct inode *inode, struct inode *dir, struct dentry *dentry, umode_t mode)
{
	lba_t lba;
	struct buffer_head *buffer;
	struct himfs_meta_block *meta_block;
	struct himfs_inode *him_inode;
	struct himfs_ino himfs_ino;
	struct himfs_inode_info *hii = HIMFS_I(inode);
	int idx;
	int i;

	lba = murmurHash3(dir->i_ino, dentry->d_name.name, dentry->d_name.len);
	lba %= META_REGIN_END_LBA;
	if (lba < META_REGIN_START_LBA) 
	{
		lba = META_REGIN_START_LBA;
	}

	buffer = sb_bread(dir->i_sb, lba);

	if (unlikely(!buffer))
	{
		printk(KERN_ERR "allocate bh for himfs_inode fail");
		return 0;
	}

	meta_block = (struct himfs_meta_block*)buffer->b_data;

	for (idx = 0; idx < HASH_SLOT_NUM; ++idx)
	{
		if (test_bit(idx, meta_block->slot_bitmap))
		{
			continue;
		}

		break;
	}

	if (idx == HASH_SLOT_NUM)
	{
		return 0;
	}

	him_inode = &meta_block->himfs_inode[idx];
	him_inode->i_mode = mode;
	himfs_ino.ino.slot = idx;
    himfs_ino.ino.hash_key = lba;
    him_inode->i_ino = himfs_ino.raw_ino;	 
    him_inode->i_uid = (uint16_t)__kuid_val(inode->i_uid);
    him_inode->i_gid = (uint16_t)__kgid_val(inode->i_gid);
    him_inode->i_size = 0;
    // him_inode->i_ctime = inode->i_ctime;
    // him_inode->i_mtime = inode->i_mtime;
    him_inode->i_crtime = hii->i_crtime;
    him_inode->i_detime = 0;
	him_inode->filename.name_len = dentry->d_name.len;
	strncpy(him_inode->filename.name, dentry->d_name.name, dentry->d_name.len);
    him_inode->i_pid = dir->i_ino;
	if (mode & S_IFREG)
	{
		for (i = 0; i < 16; ++i);
		{
			him_inode->i_block[i] = 0;
		}
	}
	set_bit(idx, meta_block->slot_bitmap);

	set_buffer_uptodate(buffer);//表示可以回写
	mark_buffer_dirty(buffer);
	brelse(buffer);

	return him_inode->i_ino;
}

static void grave_sync(struct super_block *sb, struct grave *grave)
{
	lba_t lba;
	int idx;
	struct buffer_head *buffer;
	struct himfs_meta_block *meta_block;
	struct himfs_inode *him_inode;
	struct himfs_ino himfs_ino;
	uint32_t detime;
	int i;

	for (i = 0; i < GRAVE_NUM; ++i)
	{
		himfs_ino.raw_ino = him_inode->i_pid;
		lba = himfs_ino.ino.hash_key;
		buffer = sb_bread(sb, lba);
	
		meta_block = (struct himfs_meta_block*)buffer->b_data;
		idx = himfs_ino.ino.slot;
		him_inode = &meta_block->himfs_inode[idx];

		detime = him_inode->i_grave[i].detime;
		him_inode->i_ctime = him_inode->i_mtime = detime;
		--him_inode->i_size;

		mark_buffer_dirty(buffer);
		sync_dirty_buffer(buffer);
		brelse(buffer);
	}
}

bool hash_update(struct inode *dir, struct dentry *dentry, struct inode_context *ctx)
{
	lba_t lba;
	struct buffer_head *buffer;
	struct himfs_meta_block *meta_block;
	struct himfs_inode *him_inode;
	int idx;
	int i;

	lba = murmurHash3(dir->i_ino, dentry->d_name.name, dentry->d_name.len);
	lba %= META_REGIN_END_LBA;
	if (lba < META_REGIN_START_LBA) 
	{
		lba = META_REGIN_START_LBA;
	}

	buffer = sb_sbread(dir->i_sb, lba);

	if (unlikely(!buffer))
	{
		printk(KERN_ERR "allocate bh for himfs_inode fail");
		return false;
	}

	meta_block = (struct himfs_meta_block*)buffer->b_data;

	for (idx = 0; idx < HASH_SLOT_NUM; ++idx)
	{
		if (test_bit(idx, meta_block->slot_bitmap))
		{
			him_inode = &meta_block->himfs_inode[idx];
			if (strcmp(him_inode->filename.name, dentry->d_name.name) == 0)
			{
				break;
			}
		}
	}

	if (idx == HASH_SLOT_NUM)
	{
		printk(KERN_ERR "hash_update not find\n");
		return false;
	}

	if (ctx->is_delete)
	{
		for (i = 0; i < GRAVE_NUM; ++i)
		{
			if (him_inode->i_grave[i].pid == 0)
			{
				him_inode->i_grave[i].pid = him_inode->i_pid;
				// ctx->inode->i_detime = him_inode->i_detime = him_inode->i_grave[i].detime = current_time(ctx->inode);
			}
		}

		if (i >= GRAVE_NUM - 1)
		{
			// grave_sync(dir->i_sb, &him_inode->i_grave);
			for (i = 0; i < GRAVE_NUM; ++i)
			{
				him_inode->i_grave[i].pid = 0;
			}
		}
		
		clear_bit(idx, meta_block->slot_bitmap);
		set_buffer_uptodate(buffer);//表示可以回写
		mark_buffer_dirty(buffer);
	}
	else
	{
		// TODU
	}

	brelse(buffer);
	return true;
}