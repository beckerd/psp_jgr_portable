#ifndef JGR_SAVEDATA_H
#define JGR_SAVEDATA_H
/* Main thread, after a successful story load/save: refresh XMB presentation
   in an existing save directory, using ICON0 from the specified EBOOT.
   saved!=0 updates the save timestamp. Failure never changes story/backup data.
   Paths are physical filesystem paths, outside the game's asset router. */
int savedata_update(const char *save_directory,const char *executable,int saved);
#endif
