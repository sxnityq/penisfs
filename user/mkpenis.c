/* 
    Make penis file system
*/
#include <stdio.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "../kernel/penisfs.h"

char helloworld[] = "hello world! Thank u for using penisfs";
char linkmsg[]    = "/mnt/link.txt";

/* does not make any sense */
void wipe_img(int fd, ssize_t sz){
    int blocks;
    char buffer[PENISFS_BLOCK_SZ];

    lseek(fd, 0, SEEK_SET);
    blocks = (int)(sz / PENISFS_BLOCK_SZ);

    for (int i = 0; i < blocks; i++){
        printf("i: %d\n", i);
        write(fd, buffer, PENISFS_BLOCK_SZ);
    }
}


struct penisfs_dentry *init_dir(int ino, char *name, int len, struct penisfs_dentry *dentry){
    memcpy(dentry->name, name, len);
    dentry->ino = ino;
    return dentry;
}


int write_sb(int fd){
    struct penisfs_super_block psb;
    char buffer[PENISFS_BLOCK_SZ];
    
    memset(buffer, '\0', PENISFS_BLOCK_SZ);
    memset(&psb, 0, sizeof(struct penisfs_super_block));

    psb.magic = PENIS_DO_MAGIC;
    psb.version = 228;
    //psb.imap = 0x03;
    //psb.dmap = 0x03;

    memcpy(buffer, &psb, sizeof(psb));
    lseek(fd, PENISFS_SUPER_BLOCK, SEEK_SET);

    return write(fd, buffer, PENISFS_BLOCK_SZ);
}

int write_iroot(int fd){
    struct penisfs_inode iroot, ifile1, ifile2;
    struct penisfs_inode ilink;

    struct penisfs_dentry dentry, dentry2;
    struct penisfs_dentry dot, dotdot;
    struct penisfs_dentry link;


    char buffer[PENISFS_BLOCK_SZ];

	memset(buffer, 0, PENISFS_BLOCK_SZ);

    memset(&iroot, 0, sizeof(struct penisfs_inode));
    memset(&ifile1, 0, sizeof(struct penisfs_inode));
    memset(&ifile1, 0, sizeof(struct penisfs_inode));
    memset(&ilink, 0, sizeof(struct penisfs_inode));

    memset(&dentry, 0, sizeof(struct penisfs_dentry));
    memset(&dentry2, 0, sizeof(struct penisfs_dentry));
    memset(&dot, 0, sizeof(struct penisfs_dentry));
    memset(&dotdot, 0, sizeof(struct penisfs_dentry));
    memset(&link, 0, sizeof(struct penisfs_dentry));

    iroot.i_uid = 0;
    iroot.i_gid = 0;
    iroot.datablock = PENISFS_FIRST_DATA_BLOCK;
    iroot.i_size = PENISFS_BLOCK_SZ;
    iroot.i_mode = S_IFDIR | 0755;

    ifile1.i_uid = 0;
    ifile1.i_gid = 0;
    ifile1.datablock = PENISFS_FIRST_DATA_BLOCK + 1;
    ifile1.i_size = sizeof(helloworld);
    ifile1.i_mode = S_IFREG | 0644;

    ifile2.i_uid = 0;
    ifile2.i_gid = 0;
    ifile2.datablock = PENISFS_FIRST_DATA_BLOCK + 2;
    ifile2.i_size = PENISFS_BLOCK_SZ;
    ifile2.i_mode = S_IFDIR | 0755;

    ilink.i_uid = 0;
    ilink.i_gid = 0;
    ilink.datablock = PENISFS_FIRST_DATA_BLOCK + 3;
    ilink.i_size = sizeof(linkmsg);
    ilink.i_mode = S_IFLNK | 0777;

    init_dir(ROOT_INO, ".", 2, &dot);
    init_dir(ROOT_INO, "..", 3, &dotdot);
    init_dir(ROOT_INO + 1, "a.txt", 6, &dentry);
    init_dir(ROOT_INO + 2, "bdir", 5, &dentry2);
    init_dir(ROOT_INO + 3, "link", 5, &link);


    lseek(fd, PENISFS_INODE_BLOCK * PENISFS_BLOCK_SZ, SEEK_SET);
    memcpy(((struct penisfs_inode *)buffer) + ROOT_INO, &iroot, sizeof(struct penisfs_inode));
    memcpy(((struct penisfs_inode *)buffer) + ROOT_INO + 1, &ifile1, sizeof(struct penisfs_inode));
    memcpy(((struct penisfs_inode *)buffer) + ROOT_INO + 2, &ifile2, sizeof(struct penisfs_inode));
    memcpy(((struct penisfs_inode *)buffer) + ROOT_INO + 3, &ilink, sizeof(struct penisfs_inode));
    
    write(fd, buffer, PENISFS_BLOCK_SZ);
	memset(buffer, 0, PENISFS_BLOCK_SZ);

	lseek(fd, PENISFS_FIRST_DATA_BLOCK * PENISFS_BLOCK_SZ, SEEK_SET);

    memcpy((struct penisfs_dentry *)buffer + 0, &dot, sizeof(struct penisfs_dentry));
    memcpy((struct penisfs_dentry *)buffer + 1, &dotdot, sizeof(struct penisfs_dentry));
    memcpy((struct penisfs_dentry *)buffer + 2, &dentry, sizeof(struct penisfs_dentry));
    memcpy((struct penisfs_dentry *)buffer + 3, &dentry2, sizeof(struct penisfs_dentry));
    memcpy((struct penisfs_dentry *)buffer + 4, &link, sizeof(struct penisfs_dentry));


    write(fd, buffer, PENISFS_BLOCK_SZ);

    lseek(fd, ifile1.datablock * PENISFS_BLOCK_SZ, SEEK_SET);
    write(fd, &helloworld, sizeof(helloworld));
    lseek(fd, ilink.datablock * PENISFS_BLOCK_SZ, SEEK_SET);
    write(fd, &linkmsg, sizeof(linkmsg));
};

int main(int argc, char *argv[]){
    int fd;
    struct stat sb;
    long sz;

    if (argc != 2){
        printf("usage: %s <partition>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    errno = 0;
    if ((fd = open(argv[1], O_RDWR | O_SYNC)) == -1){
        perror("open: ");
        exit(EXIT_FAILURE);
    }

    errno = 0;
    if (fstat(fd, &sb) == -1){
        perror("fstat: ");
        exit(EXIT_FAILURE);
    }

    if (!S_ISBLK(sb.st_mode)){
        printf("file is not a block device\n");
        exit(EXIT_FAILURE);
    }

    ioctl(fd, BLKGETSIZE64, &sz);
    write_sb(fd);
    write_iroot(fd);
    close(fd);
    return 0;
}