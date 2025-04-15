#ifndef FILE_SYSTEM_FILE_SYS_H
#define FILE_SYSTEM_FILE_SYS_H

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * 布局：
 *         0                 1               2             ...   999
 * +----------------+----------------+----------------+---------------+
 * |    FAT(2000B) + ROOT_FCB(48B)   | ROOT DIR FIRST |   DATA AREA   |
 * +----------------+----------------+-------------- -+---------------+
 *        1024B           1024B             1024B          ...
 */

#define BLOCK_SIZE 1024  // 块大小（字节）
#define BLOCK_ASSET 1000 // 块数量
#define DIST_SIZE (BLOCK_ASSET * BLOCK_SIZE) // 模拟磁盘大小（字节）

#define END 0XFFFF // 内容结束标志
#define FREE 0 // 盘块空闲标志

#define REAL_DATA_FILE "./data" // 实际磁盘数据文件

#define FAT_FIRST 0               // FAT 起始盘块号
#define ROOT_DIR_FIRST 2          // 根目录起始盘块号
#define DATA_START ROOT_DIR_FIRST // 数据区起始盘块号

#define ROOT_FCB_OFFSET 2000 // root_fcb 所在虚拟磁盘位置偏移量

#define MY_LS "my_ls"           // 列出当前目录命令
#define MY_EXITSYS "my_exitsys" // 退出命令
#define MY_FORMAT "my_format"   // 格式化命令
#define MY_CD "my_cd"           // 改变当前目录命令
#define MY_MKDIR "my_mkdir"     // 创建文件夹命令
#define MY_RMDIR "my_rmdir"     // 删除文件夹命令
#define MY_CREATE "my_create"   // 创建文件命令
#define MY_RM "my_rm"           // 删除文件命令

#define MIN(x, y) ((x) < (y)) ? (x) : (y)

typedef struct fcb {
    char filename[16];     // 文件名
    char ext[8];           // 扩展名
    unsigned char is_file; // 文件属性字段，0：目录文件；1：实体文件
    time_t created_time;   // 创建时间
    unsigned short len;    // 文件或文件目录大小（字节数）
    unsigned short first;  // 起始盘块号
} fcb;

void start_sys(void);

void command();

#endif //FILE_SYSTEM_FILE_SYS_H
