#include <stdio.h>   
#include <sys/types.h>   
#include <sys/stat.h>   
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
 
#define file_num 10000
const char path[16] = "/mnt/bbssd/";
 
int main()   
{   
 int fd;  
 for(int i = 0; i < file_num; i ++)
    {
        char filename[1000];
        strcpy(filename, path);
        sprintf(filename + strlen(path), "%d", i);
        //printf("%s\n", filename);
        fd = creat(filename,O_RDWR);
        close(fd);
    }
    return 0;
}