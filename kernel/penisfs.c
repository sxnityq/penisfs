#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>

#include "penisfs.h"

#define LOG_TAG "[penisfs]: "
#define pprintk(fmt, ...) printk(LOG_TAG fmt, ##__VA_ARGS__)
#define MAX_KEY_LEN     512


MODULE_AUTHOR("rxio64");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("penisfs file system");


/* super block IN MEMORY private info */
struct penisfs_sb_info {
	__u8 version;
//	__u64 imap;    /* later */
//  __u64 dmap;   /* later */
    char *key;
    __u16 key_len;
};

/* generic inode + datablock informatio IN MEMORY */
struct penisfs_inode_info {
    __u32 datablock;
    struct inode vfs_inode;
};

struct dentry *penisfs_mount(struct file_system_type *, int,
		       const char *, void *);
void penisfs_kill(struct super_block *);
int penisfs_fill_super(struct super_block *, void *, int);
struct inode *penisfs_alloc_inode(struct super_block *sb);
void destroy_inode(struct inode *inode);
int penisfs_readdir(struct file *, struct dir_context *);
struct dentry *penisfs_lookup(struct inode *,struct dentry *, unsigned int);
struct penisfs_dentry *penisfs_find_entry(struct dentry * dentry);
int penisfs_file_fsync(struct file *, loff_t, loff_t, int datasync);

ssize_t penisfs_file_read(struct file *, char __user *, size_t, loff_t *);
ssize_t penisfs_file_write(struct file *, const char __user *, size_t, loff_t *);
const char *penisfs_get_link(struct dentry *, struct inode *, struct delayed_call *);


static const struct inode_operations penisfs_dir_ops = {
    .lookup = penisfs_lookup,
    .getattr = simple_getattr,
};

static const struct file_operations penisfs_dir_fops = {
	.owner = THIS_MODULE,
    .read  = generic_read_dir,
	.iterate_shared	= penisfs_readdir,
};

static const struct inode_operations penisfs_file_ops = {
	.getattr	= simple_getattr,
};

static const struct file_operations penisfs_file_fops = {
	.read	    = penisfs_file_read,
	.write  	= penisfs_file_write,
	.fsync      = penisfs_file_fsync,
    .mmap		= generic_file_mmap,
	.llseek		= generic_file_llseek,
};

static const struct inode_operations penisfs_link_ops = {
    .get_link   = penisfs_get_link,
};

int penisfs_file_fsync(struct file *filep, loff_t start, loff_t end, int datasync){
    return generic_buffers_fsync(filep, start, end, datasync);
}

static void penisfs_free_link(void *bh)
{
	brelse(bh);
}

const char * penisfs_get_link(struct dentry *dentry, struct inode *inode, struct delayed_call *callback){
    struct buffer_head *bh;
    struct penisfs_inode_info *pii;

    pii = container_of(inode, struct penisfs_inode_info, vfs_inode);
    bh = sb_bread(inode->i_sb, pii->datablock);

    if(!bh){
        return ERR_PTR(-EIO);
    }

    set_delayed_call(callback, penisfs_free_link, bh);
    return bh->b_data;
}


/* get an inode from the cache or allocate and initialise it */
static struct inode *penisfs_iget(struct super_block *sb, unsigned long ino){
    struct inode *inode;
    struct buffer_head *bh;
    struct penisfs_inode *pi;
    struct penisfs_inode_info *ppi;
    struct timespec64 ts64;

    /*  obtain an inode from a mounted file system or
    *   allocate memory for it  
    */
    inode = iget_locked(sb, ino);

    if (inode == NULL) {
		pprintk("error aquiring inode\n");
		return ERR_PTR(-ENOMEM);
	}
	/* Return inode from cache */
	if (!(inode->i_state & I_NEW)){
		return inode;
    }

    bh = sb_bread(sb, PENISFS_INODE_BLOCK);
    pi = (struct penisfs_inode *)(bh->b_data) + ino;

    ts64 = current_time(inode);
    
    inode->i_atime_nsec = inode->i_ctime_nsec = 
    inode->i_mtime_nsec = ts64.tv_nsec;

    inode->i_atime_sec = inode->i_ctime_sec = 
    inode->i_mtime_sec = ts64.tv_sec;
    
    inode->i_size = pi->i_size;
    i_gid_write(inode, pi->i_gid);
    i_uid_write(inode, pi->i_uid);
    inode->i_mode = pi->i_mode;
    inode->i_ino = ino;

    if (S_ISDIR(inode->i_mode)){
        pprintk("inode is a dir\n");
        inode->i_fop = &penisfs_dir_fops; 
        inode->i_op = &penisfs_dir_ops;
        inc_nlink(inode);
    }

    if (S_ISREG(inode->i_mode)){
        pprintk("inode is a file\n");
        inode->i_fop = &penisfs_file_fops;
        inode->i_op = &penisfs_file_ops;
    }

    if (S_ISLNK(inode->i_mode)){
        pprintk("inode is a link\n");
        inode->i_op = &penisfs_link_ops;
    }

    ppi = container_of(inode, struct penisfs_inode_info, vfs_inode);
    ppi->datablock = pi->datablock;


	brelse(bh);

    /* clear the I_NEW state and wake up any waiters */
	unlock_new_inode(inode);
    return inode;
}

struct penisfs_dentry *penisfs_find_entry(struct dentry * dentry){

    struct inode *parinode;
    struct penisfs_inode_info *pii;
    struct buffer_head *bh;
    struct penisfs_dentry *de, *res_de;

    int i;
    res_de = NULL;

    parinode = d_inode(dentry->d_parent);
    pii = container_of(parinode, struct penisfs_inode_info, vfs_inode);
    bh = sb_bread(parinode->i_sb, pii->datablock);

    for(i = 0; i < PENIS_DIR_ENTRIES; i++){
        de = ((struct penisfs_dentry *)bh->b_data) + i;
        
        if(de->ino == 0){
            continue;
        }

        if (strncmp(dentry->d_name.name, de->name, PENISFS_MAX_FILE_NAME) == 0){
            res_de = de;
            goto done;
        }
    }

    brelse(bh);
done:
    return res_de;
}

int penisfs_sync_inode(struct inode *inode){
    struct penisfs_inode_info *pii;
    struct buffer_head *bh;
    struct penisfs_inode *pi;
    unsigned long inx;

    if (inode->i_ino > PENISFS_MAX_INODES || inode->i_ino < ROOT_INO){
        pprintk("syncing error?\n");
        return -EINVAL;
    }

    inx =  inode->i_ino;

    pii = container_of(inode, struct penisfs_inode_info, vfs_inode);
    bh = sb_bread(inode->i_sb, PENISFS_INODE_BLOCK);
    pi = ((struct penisfs_inode *) bh->b_data) + inx;
    pi->i_size = inode->i_size;

    mark_buffer_dirty(bh);
    pprintk("syncnode %d", pi->datablock);
    brelse(bh);
    return 0;
}

/* 
*   find dentry in parent directory inode 
*   struct inode *dir:      The inode of the directory being searched
*   struct dentry *dentry:  The name (as a dentry) to look up
*/
struct dentry *penisfs_lookup(struct inode *dir,
    struct dentry *dentry, unsigned int flags){

    struct super_block *sb = dir->i_sb;
	struct penisfs_dentry *de;
	struct inode *inode = NULL;

	dentry->d_op = sb->s_root->d_op;

	de = penisfs_find_entry(dentry);
	if (de != NULL) {
		pprintk("getting entry: name: %s, ino: %d\n",
			de->name, de->ino);
		inode = penisfs_iget(sb, de->ino);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}

	d_add(dentry, inode);
	pprintk("looked up dentry %s\n", de->name);

	return NULL;
    
}


int penisfs_readdir(struct file *filep, struct dir_context *ctx){
    struct inode *inode;
    struct buffer_head *bh;
    struct penisfs_inode_info *pii;
    struct penisfs_dentry *de;
    int over;

    inode = file_inode(filep);
    pii = container_of(inode, struct penisfs_inode_info, vfs_inode);
    bh = sb_bread(inode->i_sb, pii->datablock);
    

    for (;ctx->pos < PENIS_DIR_ENTRIES; ctx->pos++){
        de = ((struct penisfs_dentry *)(bh->b_data)) + ctx->pos;
        
        if (de->ino == 0){
            continue;
        }
        over = dir_emit(ctx, de->name, PENISFS_MAX_FILE_NAME, de->ino, DT_UNKNOWN);
    
        if(over){
            pprintk("Read %s from folder %s, ctx->pos: %lld\n",
				de->name,
				filep->f_path.dentry->d_name.name,
				ctx->pos);
            ctx->pos++;
			goto done;
            }
    }
done:
    pprintk("done\n");
    brelse(bh);
    return 0;                                                                   
}

ssize_t penisfs_file_read(struct file *filep, char __user *buf, 
                            size_t sz, loff_t *pos){
    struct inode *inode;
    struct penisfs_inode_info *pii;
    struct buffer_head *bh;
    loff_t to_read;

    inode = file_inode(filep);
    pii = container_of(inode, struct penisfs_inode_info, vfs_inode);
    bh = sb_bread(inode->i_sb, pii->datablock);
    
    if (!bh){
        return -ENOMEM;
    }
    
    pprintk("user addr: %p", buf);
    pprintk("ino: %ld\ndatablock: %ld", inode->i_ino, pii->datablock);
    pprintk("fpos: %ld\npos: %ld", filep->f_pos, *pos);
    pprintk("b_data: %s", (char *)bh->b_data);

    if (*pos >= inode->i_size){
        return 0;
    }

    to_read = min((loff_t)sz, inode->i_size - *pos) - 1;
    pprintk("to read: %lld", to_read);

    if (copy_to_user(buf, (char *)bh->b_data, to_read)) {
        // Handle error: not all data was copied
        return -EFAULT;
    }

    brelse(bh);
    *pos += to_read;
    return to_read;
}

ssize_t penisfs_file_write(struct file *filep, const char __user *buf, 
                        size_t sz, loff_t *pos){
    struct inode *inode;
    struct penisfs_inode_info *pii;
    struct buffer_head *bh;
    struct timespec64 ts64;

    inode = file_inode(filep);

    printk(KERN_INFO "penisfs_write: pos=%llu, sz=%zu\n", *pos, sz);
    if (*pos + sz > PENISFS_BLOCK_SZ){
        pprintk("no spec\n");
        return -ENOSPC;
    }

    pii = container_of(inode, struct penisfs_inode_info, vfs_inode);
    bh = sb_bread(inode->i_sb, pii->datablock);

    if (!bh){
        return -ENOMEM;
    }

    sb_start_write(inode->i_sb);
    
    if (copy_from_user(bh->b_data + *pos, buf, sz)){
        pprintk("eflt?\n");
        return -EFAULT;
    }
    
    if (*pos + sz > inode->i_size){
        inode->i_size = *pos + sz;
    }

    ts64 = current_time(inode);
    
    inode->i_atime_nsec = inode->i_ctime_nsec = 
    inode->i_mtime_nsec = ts64.tv_nsec;

    inode->i_atime_sec = inode->i_ctime_sec = 
    inode->i_mtime_sec = ts64.tv_sec;
    
    mark_buffer_dirty(bh);
    mark_inode_dirty(inode);
    penisfs_sync_inode(inode);
    sb_end_write(inode->i_sb);
    
    *pos += sz;   
    brelse(bh);
    pprintk("idk problem: pos %d", *pos);
    return sz;
}


void destroy_inode(struct inode *inode){
    __u64 inx;
    inx = inode->i_ino;
    kfree(container_of(inode, struct penisfs_inode_info, vfs_inode));
    pprintk("inode: %lld released\n", inx);
}

struct inode *penisfs_alloc_inode(struct super_block *sb){
    struct penisfs_inode_info *ppi;
    
    ppi = kzalloc(sizeof(struct penisfs_inode_info), GFP_KERNEL);
    
    if (ppi == NULL){
        return NULL;
    }

    inode_init_once(&ppi->vfs_inode);
    return &ppi->vfs_inode;
}

static void penisfs_put_super(struct super_block *sb)
{
	printk(KERN_DEBUG "released superblock resources\n");
    return;
}

/* struct inode *penisfs_new_inode(){

}; */

const struct super_operations penis_sbops = {
    .alloc_inode = penisfs_alloc_inode,     /* allocate memo for new inode */
    .destroy_inode = destroy_inode,         /* free memo for inode */
    .put_super = penisfs_put_super,         /* free sb private info memo */
    .statfs = simple_statfs,                /* stat */
};


/* fill super block information (on ram) */
int penisfs_fill_super(struct super_block *sb, void *data, int silent){
    struct buffer_head *bh;             /* page */
    struct penisfs_sb_info *sbi;        /* penisfs super block private info */
	struct penisfs_super_block *ps;     /* penisfs super block on disk */
	struct inode *iroot;                /* root inode */
    struct dentry *rdentry;             /* root dir entry */
    int key_cpy_len = 0;
    int ret = -EINVAL;

    sbi = kzalloc(sizeof(struct penisfs_sb_info), GFP_KERNEL);
	
    if (!sbi)
		return -ENOMEM;

    sbi->key = kzalloc(MAX_KEY_LEN, GFP_KERNEL);
   
    if(!sbi->key){
        goto out_of_keymem;
    }


    bh = sb_bread(sb, PENISFS_SUPER_BLOCK);
    ps = (struct penisfs_super_block *)(bh->b_data);

    if (ps->magic != PENIS_DO_MAGIC){
        pprintk("magic is not gathering\n");    
        goto release;
    }
    
    /* map super block private info to penisfs_sb_info struct */
    sb->s_fs_info = sbi;
    sb->s_magic = ps->magic;                /* magic */
    sb->s_op = &penis_sbops;                /* super block operations */
    sb->s_blocksize      = PAGE_SIZE;       /* block size in bytes */
    sb->s_blocksize_bits = PAGE_SHIFT;      /* block size in 1 << PAGE_SHIFT bytes */
    sb->s_maxbytes = PENISFS_BLOCK_SZ;      /* max file size = 4kb*/

    if (data == NULL){
        pprintk("Key must be specified!1!\n");
        goto release;
    }
    sbi->version = ps->version;             /* sb private version */

    key_cpy_len = strnlen((char *)data, MAX_KEY_LEN);
    if (key_cpy_len >= MAX_KEY_LEN){
        key_cpy_len = MAX_KEY_LEN - 1; 
    }
    memcpy(sbi->key, data, key_cpy_len);            /* sb private key */
    sbi->key[key_cpy_len] = '\0';
    /* sbi private key len */
    sbi->key_len = key_cpy_len;
    pprintk("key: %s\nlen: %d\n", sbi->key, sbi->key_len);


    iroot = penisfs_iget(sb, ROOT_INO);
    
    if (!iroot){
        pprintk("Bad inode\n");
        goto release;
    }

    rdentry = d_make_root(iroot);
    if(!rdentry){
        iput(iroot);
        goto release;
    }
    sb->s_root = rdentry;
    
    brelse(bh);
    return 0;

release:
    brelse(bh);
    kfree(sbi->key);
    kfree(sbi);
    return ret;
out_of_keymem:
    kfree(sbi);
    return -ENOMEM;
}

struct dentry *penisfs_mount(struct file_system_type *fstype, int flags, 
                    const char *dev_name, void *data){
    return mount_bdev(fstype, flags, dev_name, data, penisfs_fill_super);
}

void penisfs_kill(struct super_block *sb){
    return kill_block_super(sb);
}


struct file_system_type penisfs_type = {
    .owner = THIS_MODULE,
    .name = "penisfs",
    .mount = penisfs_mount,
    .kill_sb = penisfs_kill,
    .fs_flags = FS_REQUIRES_DEV
};


static int __init penisfs_init(void)
{
    int err;
    
    err = register_filesystem(&penisfs_type);
	if (err) {
		pprintk("register_filesystem failed\n");
		return err;
	}
	return 0;
}

static void __exit penisfs_exit(void)
{
    unregister_filesystem(&penisfs_type);
}

module_init(penisfs_init);
module_exit(penisfs_exit);