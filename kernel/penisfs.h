#include <linux/types.h>

#define PENIS_DO_MAGIC       0x6469636B // DICK
#define PENIS_NUM_BLOCKS        1
#define PENISFS_MAX_FILE_NAME    32
#define PENISFS_BLOCK_SZ        4096
#define PENIS_DIR_ENTRIES       32   
#define PENISFS_MAX_INODES      32

#define PENISFS_SUPER_BLOCK	0
#define PENISFS_INODE_BLOCK	1
#define PENISFS_FIRST_DATA_BLOCK	2
#define ROOT_INO    1

/*
 * Filesystem layout:
 *
 *      SB      IZONE 	     DATA
 *    ^	    ^ (1 block)
 *    |     |
 *    +-0   +-- 4096
 * 
 * 
 *  SB -> IMAP
 *  SB -> DMAP
 */

struct penisfs_super_block {
    __u8 version;
    __u32 magic;
//    __u64 imap;  /* later */
//    __u64 dmap;  /* later */
};

struct penisfs_inode {
    __u16   i_mode;         /* File mode */
    __u16   i_uid;          /* Low 16 bits of Owner Uid */
    __u16   i_gid;		    /* Low 16 bits of Group Id */
    __u32	i_size; 	    /* Size in bytes */

//	__u32	i_atime;	    /* Access time */
//	__u32	i_ctime;	    /* Inode Change time */
//	__u32	i_mtime;	    /* Modification time */

    __u32 datablock;        /* data block index (location of data) */
};

//30 bytes 18 bytes
struct penisfs_dentry {
	__u32 ino;
	char name[PENISFS_MAX_FILE_NAME];
};

