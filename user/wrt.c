#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>


char msg[] = "dimasik loot in";

int main(){
    int fd, wrt, rd;
    char buf[128];
    
    memset(buf, 0, 128);

    errno = 0;
    if ((fd = open("/mnt/penis/a.txt", O_RDWR)) == -1){
        perror("open");
        return 0;
    }
    
    rd = read(fd, buf, 128);
    printf("rd: %d\n", rd);
    printf("buf: %s\n", buf);


    lseek(fd, 0, SEEK_SET);
    wrt = write(fd, msg, sizeof(msg));
    printf("wrt: %d\n", wrt);
    lseek(fd, 0, SEEK_SET);
    
    memset(buf, 0, 128);
    rd = read(fd, buf, 128);
    printf("rd: %d\n", rd);
    printf("buf2: %s\n", buf);
    
    printf("wrt: %d\nmsg: %d\n", wrt, sizeof msg);

    return 0;
}