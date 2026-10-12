/* PSP Memory Stick replacement must not depend on rename-over-existing.
   Write/close/read back the new 1024-byte image first, then move the old
   save aside and promote to an absent name. Keep the previous valid image
   for recovery if power is lost between those two renames. */
static const char *save_operation="none";
static int save_errno;
const char *story_save_operation(void){return save_operation;}
int story_save_errno(void){return save_errno;}
static int story_io_path(char out[256],const char *path,const char *suffix)
{
    if(!path || !*path)return 0;
    int n=snprintf(out,256,"%s%s",path,suffix);
    return n>=0 && n<256;
}
static int story_read_file(const char *path,unsigned char out[1024])
{
    if(!path)return -1;
    FILE *f=fopen(path,"rb");if(!f)return -1;
    size_t n=fread(out,1,1024,f);int extra=fgetc(f),bad=ferror(f);
    if(fclose(f))bad=1;
    return n==1024 && extra==EOF && !bad ? 0:-2;
}
static int story_io_error(const char *operation,int code)
{save_operation=operation;save_errno=code ? code:EIO;return -1;}
int story_save(const char *path)
{
    char tmp[256],backup[256];unsigned char verify[1024];struct stat st;
    save_operation="path";save_errno=0;
    if(!story_io_path(tmp,path,".tmp") || !story_io_path(backup,path,".bak"))
        return story_io_error("path",EINVAL);
    FILE *f=fopen(tmp,"wb");if(!f)return story_io_error("open",errno);
    int bad=0,code=0;const char *operation="write";
    if(fwrite(story,1,sizeof story,f)!=sizeof story){bad=1;code=errno;}
    if(fflush(f) && !bad){bad=1;code=errno;operation="flush";}
    if(fclose(f) && !bad){bad=1;code=errno;operation="close";}
    if(!bad && (story_read_file(tmp,verify) || memcmp(verify,story,sizeof story))){bad=1;code=EIO;operation="verify";}
    if(bad){remove(tmp);return story_io_error(operation,code);}
    int previous=stat(path,&st)==0;
    if(!previous && errno!=ENOENT){code=errno;remove(tmp);return story_io_error("stat",code);}
    if(previous){
        /* Never replace a valid recovery image with a damaged primary. */
        if(story_read_file(path,verify)){
            if(story_read_file(backup,verify)){remove(tmp);return story_io_error("previous",EIO);}
            if(remove(path)){code=errno;remove(tmp);return story_io_error("damaged-remove",code);}
            previous=0; /* keep the already recovered backup */
        }else{
            if(remove(backup) && errno!=ENOENT){code=errno;remove(tmp);return story_io_error("backup-remove",code);}
            if(rename(path,backup)){code=errno;remove(tmp);return story_io_error("backup-rename",code);}
        }
    }
    if(rename(tmp,path)){
        code=errno;
        if(previous)rename(backup,path); /* load can recover .bak if rollback also fails */
        remove(tmp);return story_io_error("promote",code);
    }
    save_operation="complete";return 0;
}
