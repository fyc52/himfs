#ifndef _TEST_H_
#define _TEST_H_
#include "himfs_d.h"
#endif

struct himfs_inode_info* HIMFS_I(struct inode * inode);

unsigned int BKDRHash(char *str, int len);
uint32_t murmurHash3(char *str, int len);

struct buffer_head* hash_get(struct inode *dir, struct dentry *dentry, int *idx);
unsigned int hash_insert(struct inode *inode, struct inode *dir, struct dentry *dentry, umode_t mode);
bool hash_update(struct inode *dir, struct dentry *dentry, struct inode_context *ctx);