/* Worker-only stdio adapter for user-approved File System Access handles.
 * No disc or output tree is copied into Emscripten's in-memory filesystem.
 * The JS owner confines paths to the selected folder and completes each I/O
 * promise before the suspended converter resumes. */
#define _GNU_SOURCE 1
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include <emscripten.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define IO_PATH_BYTES 1024u
#define IO_BUFFER_BYTES (128u*1024u)
EM_JS(void,io_error_message,(char *out,unsigned bytes),{
    stringToUTF8(Module.browserIo.lastError || "",out,bytes);
});
const char *jgr_browser_last_io_error(void)
{
    static char message[384];
    io_error_message(message,sizeof message);return message;
}
/* Unlike libc errno values, the JS boundary uses conventional POSIX numbers
 * on every host. Emscripten's musl/WASI errno numbering differs. */
static int io_errno(int code)
{
    switch(code){
    case 1:return EPERM;case 2:return ENOENT;case 5:return EIO;
    case 9:return EBADF;case 12:return ENOMEM;case 13:return EACCES;
    case 16:return EBUSY;case 17:return EEXIST;case 20:return ENOTDIR;
    case 21:return EISDIR;case 22:return EINVAL;case 24:return EMFILE;
    case 27:return EFBIG;case 28:return ENOSPC;case 30:return EROFS;
    case 36:return ENAMETOOLONG;case 38:return ENOSYS;case 39:return ENOTEMPTY;
    default:return EIO;
    }
}
static int checked(int result)
{if(result<0){errno=io_errno(-result);return -1;}return result;}

EM_ASYNC_JS(int,io_open,(const char *path,const char *mode),{
    try { return await Module.browserIo.open(UTF8ToString(path),UTF8ToString(mode)); }
    catch (error) { return -Math.abs((Module.browserIo.translate ? Module.browserIo.translate(error) : error).errno || 5); }
});
EM_ASYNC_JS(int,io_read,(int fd,void *data,unsigned bytes),{
    try {
        const value=await Module.browserIo.read(fd,bytes);
        if (!(value instanceof Uint8Array) || value.length>bytes) return -5;
        HEAPU8.set(value,data);return value.length;
    } catch (error) { return -Math.abs((Module.browserIo.translate ? Module.browserIo.translate(error) : error).errno || 5); }
});
EM_ASYNC_JS(int,io_write,(int fd,const void *data,unsigned bytes),{
    try {
        const value=HEAPU8.slice(data,data+bytes);
        const written=await Module.browserIo.write(fd,value);
        return Number.isSafeInteger(written) && written>=0 && written<=bytes ? written : -5;
    } catch (error) { return -Math.abs((Module.browserIo.translate ? Module.browserIo.translate(error) : error).errno || 5); }
});
EM_ASYNC_JS(double,io_seek,(int fd,double offset,int whence),{
    try {
        const position=await Module.browserIo.seek(fd,offset,whence);
        return Number.isSafeInteger(position) && position>=0 ? position : -5;
    } catch (error) { return -Math.abs((Module.browserIo.translate ? Module.browserIo.translate(error) : error).errno || 5); }
});
EM_ASYNC_JS(int,io_close,(int fd),{
    try { await Module.browserIo.close(fd);return 0; }
    catch (error) { return -Math.abs((Module.browserIo.translate ? Module.browserIo.translate(error) : error).errno || 5); }
});
EM_ASYNC_JS(int,io_stat,(const char *path,int fd,double *values),{
    try {
        const entry=fd>=0 && Module.browserIo.statFd ? await Module.browserIo.statFd(fd) :
            await Module.browserIo.stat(UTF8ToString(path));
        if (!entry || !Number.isSafeInteger(entry.size) || entry.size<0 ||
            !Number.isSafeInteger(entry.mode) || !Number.isFinite(entry.mtime)) return -5;
        HEAPF64[values/8]=entry.size;HEAPF64[values/8+1]=entry.mode;
        HEAPF64[values/8+2]=entry.mtime;return 0;
    } catch (error) { return -Math.abs((Module.browserIo.translate ? Module.browserIo.translate(error) : error).errno || 5); }
});
EM_ASYNC_JS(int,io_mkdir,(const char *path),{
    try { await Module.browserIo.mkdir(UTF8ToString(path));return 0; }
    catch (error) { return -Math.abs((Module.browserIo.translate ? Module.browserIo.translate(error) : error).errno || 5); }
});
EM_ASYNC_JS(int,io_remove,(const char *path),{
    try { await Module.browserIo.remove(UTF8ToString(path));return 0; }
    catch (error) { return -Math.abs((Module.browserIo.translate ? Module.browserIo.translate(error) : error).errno || 5); }
});
EM_ASYNC_JS(int,io_rename,(const char *from,const char *to),{
    try { await Module.browserIo.rename(UTF8ToString(from),UTF8ToString(to));return 0; }
    catch (error) { return -Math.abs((Module.browserIo.translate ? Module.browserIo.translate(error) : error).errno || 5); }
});
EM_ASYNC_JS(int,io_list,(const char *path),{
    try {
        const entries=await Module.browserIo.list(UTF8ToString(path));
        if (!Array.isArray(entries)) return -5;
        if (!Module.browserDirectories) {
            Module.browserDirectories=new Map();Module.browserDirectoryId=0;
        }
        const id=++Module.browserDirectoryId;
        Module.browserDirectories.set(id,{entries:entries,index:0});return id;
    } catch (error) { return -Math.abs((Module.browserIo.translate ? Module.browserIo.translate(error) : error).errno || 5); }
});
EM_JS(int,io_next,(int id,char *name,unsigned bytes,unsigned *mode),{
    const directory=Module.browserDirectories && Module.browserDirectories.get(id);
    if (!directory) return -9;
    if (directory.index>=directory.entries.length) return 0;
    const entry=directory.entries[directory.index++];
    if (!entry || typeof entry.name!=='string' || !entry.name ||
        entry.name.includes('/') || entry.name.includes('\0') ||
        lengthBytesUTF8(entry.name)>=bytes) return -36;
    stringToUTF8(entry.name,name,bytes);HEAPU32[mode/4]=entry.mode||0;return 1;
});
EM_JS(void,io_list_close,(int id),{
    if (Module.browserDirectories) Module.browserDirectories.delete(id);
});

static char current_directory[IO_PATH_BYTES]="/";
static int normalized(const char *path,char out[IO_PATH_BYTES])
{
    if(!path || !*path){errno=ENOENT;return -1;}
    char joined[IO_PATH_BYTES];int n;
    if(path[0]=='/')n=snprintf(joined,sizeof joined,"%s",path);
    else n=snprintf(joined,sizeof joined,"%s/%s",current_directory,path);
    if(n<0 || (size_t)n>=sizeof joined){errno=ENAMETOOLONG;return -1;}
    size_t length=1;out[0]='/';out[1]=0;
    const char *p=joined;
    while(*p){
        while(*p=='/')p++;
        const char *begin=p;while(*p && *p!='/')p++;
        size_t bytes=(size_t)(p-begin);if(!bytes || (bytes==1 && begin[0]=='.'))continue;
        if(bytes==2 && begin[0]=='.' && begin[1]=='.'){
            while(length>1 && out[length-1]!='/')length--;
            if(length>1)length--;out[length]=0;continue;
        }
        if(length>1)out[length++]='/';
        memcpy(out+length,begin,bytes);length+=bytes;out[length]=0;
    }
    return 0;
}
static uint32_t path_inode(const char *path)
{uint32_t value=2166136261u;while(*path){value^=(unsigned char)*path++;value*=16777619u;}return value?value:1;}
static int metadata(const char *path,int fd,struct stat *out)
{
    if(!out){errno=EINVAL;return -1;}
    double data[3];
    if(!strcmp(path,"/")){data[0]=0;data[1]=S_IFDIR|0755;data[2]=0;}
    else if(checked(io_stat(path,fd,data))<0)return -1;
    if(data[0]>9007199254740991. || data[2]<0 || data[2]>9007199254740991.){errno=EOVERFLOW;return -1;}
    memset(out,0,sizeof *out);out->st_mode=(mode_t)data[1];out->st_size=(off_t)data[0];
    out->st_mtime=out->st_ctime=out->st_atime=(time_t)data[2];
    out->st_ino=path_inode(path);out->st_dev=1;out->st_nlink=1;out->st_blksize=4096;
    out->st_blocks=(out->st_size+511)/512;return 0;
}
int __wrap_stat(const char *path,struct stat *out)
{char absolute[IO_PATH_BYTES];return normalized(path,absolute)?-1:metadata(absolute,-1,out);}
int __wrap_lstat(const char *path,struct stat *out){return __wrap_stat(path,out);}
int __wrap_mkdir(const char *path,mode_t mode)
{char absolute[IO_PATH_BYTES];(void)mode;return normalized(path,absolute)?-1:checked(io_mkdir(absolute));}
int __wrap_remove(const char *path)
{char absolute[IO_PATH_BYTES];return normalized(path,absolute)?-1:checked(io_remove(absolute));}
int __wrap_unlink(const char *path){return __wrap_remove(path);}
int __wrap_rename(const char *from,const char *to)
{char a[IO_PATH_BYTES],b[IO_PATH_BYTES];return normalized(from,a)||normalized(to,b)?-1:checked(io_rename(a,b));}
int __wrap_access(const char *path,int mode)
{struct stat st;(void)mode;return __wrap_stat(path,&st);}
int __wrap_chdir(const char *path)
{
    char absolute[IO_PATH_BYTES];struct stat st;
    if(normalized(path,absolute) || metadata(absolute,-1,&st))return -1;
    if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return -1;}
    strcpy(current_directory,absolute);return 0;
}
char *__wrap_getcwd(char *out,size_t bytes)
{
    size_t needed=strlen(current_directory)+1;
    if(!out){if(!bytes)bytes=needed;out=malloc(bytes);if(!out)return NULL;
        if(bytes<needed){free(out);errno=ERANGE;return NULL;}}
    else if(bytes<needed){errno=ERANGE;return NULL;}
    memcpy(out,current_directory,needed);return out;
}
char *__wrap_realpath(const char *path,char *out)
{
    char absolute[IO_PATH_BYTES];struct stat st;
    if(normalized(path,absolute) || metadata(absolute,-1,&st))return NULL;
    if(!out){out=malloc(strlen(absolute)+1);if(!out)return NULL;}
    strcpy(out,absolute);return out;
}

typedef struct BrowserFile {
    struct BrowserFile *next;
    FILE *stream;
    int fd,readable,writable;
    char *buffer;
    char path[IO_PATH_BYTES];
} BrowserFile;
static BrowserFile *files;
static ssize_t stream_read(void *cookie,char *data,size_t bytes)
{BrowserFile *file=cookie;if(bytes>INT_MAX)bytes=INT_MAX;return checked(io_read(file->fd,data,(unsigned)bytes));}
static ssize_t stream_write(void *cookie,const char *data,size_t bytes)
{
    BrowserFile *file=cookie;if(bytes>INT_MAX)bytes=INT_MAX;
    size_t written=0;
    while(written<bytes){
        int n=checked(io_write(file->fd,data+written,(unsigned)(bytes-written)));
        if(n<0)return -1;
        if(!n){errno=EIO;return -1;}
        written+=(unsigned)n;
    }
    return (ssize_t)written;
}
static int stream_seek(void *cookie,off_t *offset,int whence)
{
    BrowserFile *file=cookie;double position=io_seek(file->fd,(double)*offset,whence);
    if(position<0){errno=io_errno((int)-position);return -1;}
    if(position>9007199254740991.){errno=EOVERFLOW;return -1;}
    *offset=(off_t)position;return 0;
}
static int stream_close(void *cookie)
{
    BrowserFile *file=cookie;int result=checked(io_close(file->fd));
    BrowserFile **at=&files;while(*at && *at!=file)at=&(*at)->next;if(*at)*at=file->next;
    free(file->buffer);free(file);return result;
}
FILE *__wrap_fopen(const char *path,const char *mode)
{
    char absolute[IO_PATH_BYTES];if(normalized(path,absolute))return NULL;
    BrowserFile *file=calloc(1,sizeof *file);if(!file)return NULL;
    int fd=checked(io_open(absolute,mode));if(fd<0){free(file);return NULL;}
    file->fd=fd;file->readable=mode[0]=='r'||strchr(mode,'+');
    file->writable=mode[0]!='r'||strchr(mode,'+');strcpy(file->path,absolute);
    cookie_io_functions_t functions={stream_read,stream_write,stream_seek,stream_close};
    FILE *stream=fopencookie(file,mode,functions);
    if(!stream){int saved=errno;(void)io_close(fd);free(file);errno=saved;return NULL;}
    file->stream=stream;file->next=files;files=file;
    file->buffer=malloc(IO_BUFFER_BYTES);
    if(file->buffer && setvbuf(stream,file->buffer,_IOFBF,IO_BUFFER_BYTES)){
        free(file->buffer);file->buffer=NULL;
    }
    return stream;
}
extern int __real_fileno(FILE *);
extern int __real_fstat(int,struct stat *);
int __wrap_fileno(FILE *stream)
{for(BrowserFile *file=files;file;file=file->next)if(file->stream==stream)return file->fd;return __real_fileno(stream);}
int __wrap_fstat(int fd,struct stat *out)
{for(BrowserFile *file=files;file;file=file->next)if(file->fd==fd)return metadata(file->path,fd,out);return __real_fstat(fd,out);}

/* Native mission/controller readers use the PSP host shim's POSIX descriptor
 * API. Keep them on the same selected-folder backend as stdio; otherwise they
 * silently read Emscripten's unrelated MEMFS instead of the prepared assets. */
static BrowserFile *descriptor(int fd)
{for(BrowserFile *file=files;file;file=file->next)if(file->fd==fd)return file;return NULL;}
int __wrap_open(const char *path,int flags,...)
{
    char absolute[IO_PATH_BYTES];if(normalized(path,absolute))return -1;
    int access=flags&O_ACCMODE;
    if(access!=O_RDONLY && access!=O_WRONLY && access!=O_RDWR){errno=EINVAL;return -1;}
    if((flags&O_TRUNC) && access==O_RDONLY){errno=EINVAL;return -1;}
    int supported=O_ACCMODE|O_CREAT|O_EXCL|O_TRUNC|O_APPEND|O_CLOEXEC|O_LARGEFILE|O_NONBLOCK|O_NOCTTY;
    if(flags&~supported){errno=EOPNOTSUPP;return -1;}
    int exists=1;
    if((flags&O_CREAT) || (flags&O_TRUNC) || (flags&O_APPEND)){
        struct stat st;
        if(metadata(absolute,-1,&st)){
            if(errno!=ENOENT)return -1;
            exists=0;if(!(flags&O_CREAT))return -1;
        }else if((flags&(O_CREAT|O_EXCL))==(O_CREAT|O_EXCL)){errno=EEXIST;return -1;}
    }
    const char *mode=!exists || (flags&O_TRUNC)?"w+b":
        (flags&O_APPEND)?"a+b":access==O_RDONLY?"rb":"r+b";
    BrowserFile *file=calloc(1,sizeof *file);if(!file)return -1;
    int fd=checked(io_open(absolute,mode));if(fd<0){free(file);return -1;}
    file->fd=fd;file->readable=access!=O_WRONLY;file->writable=access!=O_RDONLY;
    strcpy(file->path,absolute);file->next=files;files=file;return fd;
}
extern ssize_t __real_read(int,void *,size_t);
extern ssize_t __real_write(int,const void *,size_t);
extern off_t __real_lseek(int,off_t,int);
extern int __real_close(int);
ssize_t __wrap_read(int fd,void *data,size_t bytes)
{
    BrowserFile *file=descriptor(fd);if(!file)return __real_read(fd,data,bytes);
    if(!file->readable){errno=EBADF;return -1;}
    if(bytes>INT_MAX)bytes=INT_MAX;return checked(io_read(fd,data,(unsigned)bytes));
}
ssize_t __wrap_write(int fd,const void *data,size_t bytes)
{
    BrowserFile *file=descriptor(fd);if(!file)return __real_write(fd,data,bytes);
    if(!file->writable){errno=EBADF;return -1;}
    if(bytes>INT_MAX)bytes=INT_MAX;return checked(io_write(fd,data,(unsigned)bytes));
}
off_t __wrap_lseek(int fd,off_t offset,int whence)
{
    BrowserFile *file=descriptor(fd);if(!file)return __real_lseek(fd,offset,whence);
    if(offset>9007199254740991LL || offset< -9007199254740991LL){errno=EOVERFLOW;return -1;}
    return stream_seek(file,&offset,whence)?-1:offset;
}
int __wrap_close(int fd)
{
    BrowserFile *file=descriptor(fd);if(!file)return __real_close(fd);
    /* Closing fileno(FILE) before fclose is outside the converter's contract. */
    if(file->stream){errno=EINVAL;return -1;}
    return stream_close(file);
}

typedef struct {int id;struct dirent entry;} BrowserDirectory;
DIR *__wrap_opendir(const char *path)
{
    char absolute[IO_PATH_BYTES];if(normalized(path,absolute))return NULL;
    BrowserDirectory *directory=calloc(1,sizeof *directory);if(!directory)return NULL;
    int id=checked(io_list(absolute));if(id<0){free(directory);return NULL;}
    directory->id=id;return (DIR *)directory;
}
struct dirent *__wrap_readdir(DIR *opaque)
{
    if(!opaque){errno=EBADF;return NULL;}
    BrowserDirectory *directory=(BrowserDirectory *)opaque;unsigned mode;
    int status=checked(io_next(directory->id,directory->entry.d_name,sizeof directory->entry.d_name,&mode));
    if(status<=0)return NULL;
    directory->entry.d_ino=path_inode(directory->entry.d_name);
    directory->entry.d_reclen=sizeof directory->entry;
    directory->entry.d_type=(mode&S_IFMT)==S_IFDIR?DT_DIR:DT_REG;return &directory->entry;
}
int __wrap_closedir(DIR *opaque)
{
    if(!opaque){errno=EBADF;return -1;}
    BrowserDirectory *directory=(BrowserDirectory *)opaque;io_list_close(directory->id);free(directory);return 0;
}
