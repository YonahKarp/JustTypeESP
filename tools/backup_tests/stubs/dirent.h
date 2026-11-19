#pragma once
#include <filesystem>
#include <cstring>
#include <sys/stat.h>
#ifndef S_ISREG
#define S_ISREG(mode) (((mode)&_S_IFMT)==_S_IFREG)
#define S_ISDIR(mode) (((mode)&_S_IFMT)==_S_IFDIR)
#endif
struct dirent{char d_name[512];};
struct DIR{std::filesystem::directory_iterator iterator,end;dirent entry;bool first=true;};
inline DIR* opendir(const char* path){try{auto* d=new DIR;d->iterator=std::filesystem::directory_iterator(path);return d;}catch(...){return nullptr;}}
inline dirent* readdir(DIR* d){if(!d->first&&d->iterator!=d->end)++d->iterator;d->first=false;if(d->iterator==d->end)return nullptr;auto name=d->iterator->path().filename().u8string();std::strncpy(d->entry.d_name,name.c_str(),511);d->entry.d_name[511]=0;return &d->entry;}
inline int closedir(DIR* d){delete d;return 0;}
