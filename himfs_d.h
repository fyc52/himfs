
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include <linux/fscache.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/bitmap.h>
#include <linux/dcache.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/bitmap.h>
#include <linux/uidgid.h>
#include <linux/types.h>

typedef __u64 lba_t;
typedef __u32 himfs_ino_t;


/* helpful if this is different than other fs */
#define MAX_FILE_TYPE_NAME 256
#define HIMFS_MAGIC 0x73616d70 /* "HIMFS" */
#define PAGE_SHIFT 12
#define BLOCK_SHIFT 12
#define BLOCK_SIZE 1 << BLOCK_SHIFT
#define FILE_MAX_SIZE (1 << 30)
#define HIMFS_BSTORE_BLOCKSIZE BLOCK_SIZE
#define HIMFS_BSTORE_BLOCKSIZE_BITS BLOCK_SHIFT

#define HIMFS_ROOT_INO 8
#define INVALID_INO 0U
#define INIT_SPACE 10


#define HIMFS_MAX_FILENAME_LEN 126

/* block refers to file ohimfset */
#define META_REGIN_START_LBA 1
#define META_REGIN_BITS  22
#define META_REGIN_END_LBA (1 << META_REGIN_BITS)
#define HASH_SLOT_BITS 3
#define HASH_SLOT_NUM (1 << HASH_SLOT_BITS)

#define DATA_REGIN_BITS (META_REGIN_BITS + 5)
#define DATA_REGIN_START_LBA (META_REGIN_END_LBA + 1)
#define DATA_REGIN_END_LBA (1 << DATA_REGIN_BITS)

#define GRAVE_NUM 4

struct himfs_ino 
{
    union {
        struct {
            uint32_t slot : HASH_SLOT_BITS;        /* 哈希槽位 */
            uint32_t hash_key : META_REGIN_BITS;   /* 哈希键 */
            uint32_t padding : 32 - HASH_SLOT_BITS - META_REGIN_BITS; /* 填充位 */
        } ino;
        himfs_ino_t raw_ino;  /* 原始的inode编号 */
    };
};

struct himfs_name 
{
	__u16	name_len;		/* Name length */
	char	name[HIMFS_MAX_FILENAME_LEN];			/* Dir name */
};

struct grave
{
    uint32_t pid;
    uint32_t detime;
};

struct inode_context
{
    bool is_delete;
    struct inode *inode;
};


struct himfs_inode         // 磁盘inode
{		 
    uint16_t i_mode;
    himfs_ino_t i_ino;	 
    uint16_t i_uid;
    uint16_t i_gid;
    uint32_t i_size;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_crtime;
    uint32_t i_detime;
    struct himfs_name filename;
    uint32_t i_pid;
    struct grave i_grave[GRAVE_NUM];
    uint32_t i_block[16];
    char rsv[253];
};

struct himfs_meta_block
{
    DECLARE_BITMAP(slot_bitmap, HASH_SLOT_NUM);
    struct himfs_inode himfs_inode[HASH_SLOT_NUM];
    char rsv[7];
};

struct himfs_inode_info   // 内存文件系统特化inode
{					   
    struct inode vfs_inode;
    // struct himfs_name filename;
    // bool is_dir;
    // uint32_t i_pid;
    // bool is_create_op;
    uint32_t i_crtime;
    uint32_t i_detime;
};

static inline struct himfs_sb_info *HIMFS_SB(struct super_block *sb)
{
	return sb->s_fs_info; //文件系统特殊信息
}

struct himfs_inode_info *HIMFS_I(struct inode *inode)
{
	return container_of(inode, struct himfs_inode_info, vfs_inode);
}

struct himfs_sb_info
{
    char fs_name[MAX_FILE_TYPE_NAME];
};

static inline int my_strlen(char *name)
{
    int len = 0;
    while(name[len] != '\0') len ++;
    return len;
}

static struct kmem_cache * himfs_inode_cachep;
extern struct inode_operations himfs_dir_inode_ops;
extern struct inode_operations himfs_file_inode_ops;
extern struct file_operations himfs_file_file_ops;
extern struct address_space_operations himfs_aops;
extern struct file_operations himfs_dir_operations;
static inline struct buffer_head *sb_bread(struct super_block *sb, sector_t block);
extern void brelse(struct buffer_head *bh);
extern void set_buffer_uptodate(struct buffer_head *bh);
extern void mark_buffer_dirty(struct buffer_head *bh);
extern void unlock_buffer(struct buffer_head *bh);
extern void lock_buffer(struct buffer_head *bh);
extern struct dentry *d_make_root(struct inode *root_inode);
