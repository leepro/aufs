#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "aufs.h"
#include "inode.h"
#include "super.h"

static void aufs_put_super(struct super_block *sb)
{
	struct aufs_super_block *asb = (struct aufs_super_block *)sb->s_fs_info;
	if (asb)
		kfree(asb);
	sb->s_fs_info = NULL;
	pr_debug("aufs super block destroyed\n");
}

static struct super_operations const aufs_super_ops = {
	.alloc_inode = aufs_inode_alloc,
	.destroy_inode = aufs_inode_free,
	.put_super = aufs_put_super,
};

static inline void aufs_super_block_fill(struct aufs_super_block *asb,
			struct aufs_disk_super_block const *dsb)
{
	asb->asb_magic = be32_to_cpu(dsb->dsb_magic);
	asb->asb_inode_blocks = be32_to_cpu(dsb->dsb_inode_blocks);
	asb->asb_block_size = be32_to_cpu(dsb->dsb_block_size);
	asb->asb_root_inode = be32_to_cpu(dsb->dsb_root_inode);
	asb->asb_inodes_in_block =
		asb->asb_block_size / sizeof(struct aufs_disk_inode);
}

static struct aufs_super_block *aufs_super_block_read(struct super_block *sb)
{
	struct aufs_super_block *asb = (struct aufs_super_block *)
				kzalloc(sizeof(struct aufs_super_block),
							GFP_NOFS);
	struct aufs_disk_super_block *dsb = NULL;
	struct buffer_head *bh = NULL;

	if (!asb) {
		pr_err("aufs cannot allocate super block\n");
		return NULL;
	}

	bh = sb_bread(sb, 0);
	if (!bh) {
		pr_err("cannot read 0 block\n");
		goto free_memory;
	}

	dsb = (struct aufs_disk_super_block *)bh->b_data;
	aufs_super_block_fill(asb, dsb);
	brelse(bh);

	if (asb->asb_magic != AUFS_MAGIC) {
		pr_err("wrong magic number %u\n", (unsigned)asb->asb_magic);
		goto free_memory;
	}

	pr_debug("aufs super block info:\n"
				"\tmagic           = %u\n"
				"\tinode blocks    = %u\n"
				"\tblock size      = %u\n"
				"\troot inode      = %u\n"
				"\tinodes in block = %u\n",
				(unsigned)asb->asb_magic,
				(unsigned)asb->asb_inode_blocks,
				(unsigned)asb->asb_block_size,
				(unsigned)asb->asb_root_inode,
				(unsigned)asb->asb_inodes_in_block);

	return asb;

free_memory:
	kfree(asb);
	return NULL;
}

static int aufs_fill_sb(struct super_block *sb, void *data, int silent)
{
	struct inode *root = NULL;
	struct aufs_super_block *asb = aufs_super_block_read(sb);

	if (!asb)
		return -EINVAL;

	sb->s_magic = asb->asb_magic;
	sb->s_fs_info = asb;
	sb->s_op = &aufs_super_ops;

	if (sb_set_blocksize(sb, asb->asb_block_size) == 0) {
		pr_err("device does not support block size %u\n",
					(unsigned)asb->asb_block_size);
		return -EINVAL;
	}

	root = aufs_inode_get(sb, asb->asb_root_inode);
	if (IS_ERR(root))
		return PTR_ERR(root);

	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		pr_err("aufs cannot create root\n");
		return -ENOMEM;
	}

	return 0;
}

static struct dentry *aufs_mount(struct file_system_type *type, int flags,
			char const *dev, void *data)
{
	struct dentry *const entry = mount_bdev(type, flags, dev,
				data, aufs_fill_sb);
	if (IS_ERR(entry))
		pr_err("aufs mounting failed\n");
	else
		pr_debug("aufs mounted\n");
	return entry;
}

static struct file_system_type aufs_type = {
	.owner = THIS_MODULE,
	.name = "aufs",
	.mount = aufs_mount,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV
};

static int __init aufs_init(void)
{
	int ret = aufs_inode_cache_create();
	if (ret != 0) {
		pr_err("cannot create inode cache\n");
		return ret;
	}

	ret = register_filesystem(&aufs_type);
	if (ret != 0) {
		aufs_inode_cache_destroy();
		pr_err("cannot register filesystem\n");
		return ret;
	}

	pr_debug("aufs module loaded\n");

	return 0;
}

static void __exit aufs_fini(void)
{
	int const ret = unregister_filesystem(&aufs_type);
	if (ret != 0)
		pr_err("cannot unregister filesystem\n");

	aufs_inode_cache_destroy();

	pr_debug("aufs module unloaded\n");
}

module_init(aufs_init);
module_exit(aufs_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kmu");
