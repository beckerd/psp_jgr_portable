#define _GNU_SOURCE 1
#include "portable_scratch.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char cwd[1024];assert(getcwd(cwd,sizeof cwd)&&!strcmp(cwd,"/"));
    assert(!chdir("/game"));assert(getcwd(cwd,sizeof cwd)&&!strcmp(cwd,"/game"));
    assert(!mkdir("data",0755));errno=0;assert(mkdir("data",0755)==-1&&errno==EEXIST);
    assert(!mkdir("data/cache",0755));assert(!mkdir("data/assets",0755));
    errno=0;assert(!fopen("missing.bin","rb")&&errno==ENOENT);
    FILE *f=fopen("data/assets/test.bin","w+b");assert(f);
    assert(fwrite("abcdef",1,6,f)==6);assert(!fflush(f));
    struct stat st;assert(!fstat(fileno(f),&st)&&st.st_size==6);
    assert(!fseek(f,-3,SEEK_END));assert(fwrite("XYZ",1,3,f)==3);
    assert(!fseek(f,0,SEEK_SET));char value[64]={0};assert(fread(value,1,6,f)==6&&!memcmp(value,"abcXYZ",6));
    assert(!fseek(f,12,SEEK_SET));assert(fputc('!',f)=='!');assert(!fflush(f));
    assert(!fseek(f,6,SEEK_SET));assert(fread(value,1,7,f)==7);
    for(unsigned i=0;i<6;i++)assert(!value[i]);assert(value[6]=='!');
    assert(!fclose(f));assert(!stat("./data/cache/../assets/test.bin",&st)&&st.st_size==13);
    char *resolved=realpath("data/assets/test.bin",NULL);assert(resolved&&!strcmp(resolved,"/game/data/assets/test.bin"));free(resolved);
    f=fopen("data/assets/test.bin","a+b");assert(f);assert(!fseek(f,0,SEEK_SET));
    assert(fwrite("END",1,3,f)==3);assert(!fclose(f));assert(!stat("data/assets/test.bin",&st)&&st.st_size==16);
    assert(!rename("data/assets/test.bin","data/assets/published.bin"));
    /* Native mission/controller loaders use open/read/lseek/close, not FILE. */
    int fd=open("data/assets/published.bin",O_RDONLY,0777);assert(fd>=0);
    assert(!fstat(fd,&st)&&st.st_size==16);assert(lseek(fd,0,SEEK_END)==16);
    assert(lseek(fd,0,SEEK_SET)==0);assert(read(fd,value,6)==6&&!memcmp(value,"abcXYZ",6));
    assert(lseek(fd,-3,SEEK_END)==13);assert(read(fd,value,8)==3&&!memcmp(value,"END",3));
    assert(read(fd,value,8)==0);errno=0;assert(write(fd,"!",1)==-1&&errno==EBADF);
    errno=0;assert(lseek(fd,-1,SEEK_SET)==-1&&errno==EINVAL);assert(!close(fd));
    errno=0;assert(read(fd,value,1)==-1&&errno==EBADF);
    errno=0;assert(open("missing.bin",O_RDONLY,0777)==-1&&errno==ENOENT);
    fd=open("data/assets/descriptor.bin",O_RDWR|O_CREAT|O_EXCL,0666);assert(fd>=0);
    assert(write(fd,"abcdef",6)==6);assert(lseek(fd,-3,SEEK_CUR)==3);
    assert(write(fd,"XYZ",3)==3);assert(lseek(fd,0,SEEK_SET)==0);
    assert(read(fd,value,6)==6&&!memcmp(value,"abcXYZ",6));assert(!close(fd));
    errno=0;assert(open("data/assets/descriptor.bin",O_WRONLY|O_CREAT|O_EXCL,0666)==-1&&errno==EEXIST);
    fd=open("data/assets/descriptor.bin",O_WRONLY|O_APPEND);assert(fd>=0);
    assert(lseek(fd,0,SEEK_SET)==0);assert(write(fd,"END",3)==3);
    errno=0;assert(read(fd,value,1)==-1&&errno==EBADF);assert(!close(fd));
    assert(!stat("data/assets/descriptor.bin",&st)&&st.st_size==9);
    fd=open("data/assets/descriptor.bin",O_WRONLY|O_TRUNC);assert(fd>=0);
    assert(!fstat(fd,&st)&&st.st_size==0);assert(!close(fd));assert(!remove("data/assets/descriptor.bin"));
    fd=open("data/assets/full.bin",O_WRONLY|O_CREAT,0666);assert(fd>=0);
    errno=0;assert(write(fd,"disk full",9)==-1&&errno==ENOSPC);assert(!close(fd));
    assert(!remove("data/assets/full.bin"));
    fd=open("data/assets/close-error.bin",O_WRONLY|O_CREAT,0666);assert(fd>=0);
    assert(write(fd,"close failure",13)==13);errno=0;assert(close(fd)==-1&&errno==EIO);
    assert(!remove("data/assets/close-error.bin"));
    errno=0;assert(stat("data/assets/test.bin",&st)==-1&&errno==ENOENT);
    DIR *dir=opendir("data/assets");assert(dir);unsigned count=0;struct dirent *entry;
    while((entry=readdir(dir)))if(!strcmp(entry->d_name,"published.bin")){count++;assert(entry->d_type==DT_REG);}
    assert(count==1&&!closedir(dir));assert(!access("data/assets/published.bin",F_OK));
    assert(!remove("data/assets/published.bin"));errno=0;assert(remove("data/assets/published.bin")==-1&&errno==ENOENT);
    f=fopen("data/assets/full.bin","wb");assert(f);assert(fputs("disk full",f)>=0);
    errno=0;assert(fflush(f)==EOF&&ferror(f)&&errno==ENOSPC);(void)fclose(f);
    assert(!remove("data/assets/full.bin"));
    f=fopen("data/assets/close-error.bin","wb");assert(f);assert(fputs("close failure",f)>=0);
    errno=0;assert(fclose(f)==EOF&&errno==EIO);assert(!remove("data/assets/close-error.bin"));
    /* Musl's ordinary memory streams must not be redirected to browser I/O. */
    char memory[32]={0};f=fmemopen(memory,sizeof memory,"w+");assert(f);
    assert(fputs("memory",f)>=0&&!fclose(f)&&!strcmp(memory,"memory"));
    /* The same scratch cookie ABI covers resident and spilled streams. */
    JgrScratchPool pool;jgr_scratch_init(&pool,4096);
    f=jgr_scratch_open(&pool,"/game/data/cache/spill.bin");assert(f);
    for(unsigned i=0;i<8192;i++)assert(fputc((int)(i&255),f)!=(int)EOF);
    assert(!fflush(f)&&pool.spills==1);
    for(unsigned i=0;i<8192;i+=97){assert(!fseek(f,(long)i,SEEK_SET));assert(fgetc(f)==(int)(i&255));}
    assert(!jgr_scratch_discard(&pool,f)&&!pool.streams&&!pool.allocated);
    errno=0;assert(stat("data/cache/spill.bin",&st)==-1&&errno==ENOENT);
    jgr_scratch_init(&pool,0);f=jgr_scratch_open(&pool,"/game/data/cache/full.bin");assert(f);
    assert(fputs("scratch disk full",f)>=0);errno=0;
    assert(fflush(f)==EOF&&ferror(f)&&errno==ENOSPC);
    (void)jgr_scratch_discard(&pool,f);assert(!pool.streams&&!pool.allocated);
    puts("Browser stdio and scratch Asyncify probe passed");return 0;
}
