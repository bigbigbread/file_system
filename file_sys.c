#include "file_sys.h"

char *dist; // 模拟磁盘

block0 blk0; // 引导块

unsigned short fat[BLOCK_ASSET]; // FAT

fcb cur_dir[20]; // 当前目录
size_t cur_dir_size = 0;

fcb fcb_stack[20]; // FCB 栈结构，用于存放每个层级
size_t fcb_stack_size = 0;

char cmd_arg[36]; // 输入命令
char cmd_args[16][16]; // 以空格（可多个连续空格）分隔 cmd_arg
size_t cmd_args_size = 0;

static void persistence(void);

static void sys_exit(void);

static void print_path(void);

static void my_ls();

static void my_format();

static void my_cd();

static void my_mkdir();

static void my_rmdir();

static void my_create();

static void my_rm();

static int parse_path(const char src[16], char dest[16][16], size_t *dest_size_ptr);

static void get_data_from_dist(void *dest, unsigned short first_block, size_t n);

static int get_fcb_from(fcb *dir_fcb_ptr, char filename[16], unsigned char is_file, fcb *fcb_ptr);

static unsigned short next_free_block(void);

static int create_fcb_in(fcb *dir_fcb_ptr, char *name, fcb *fcb_ptr, unsigned char is_file);

static void rmfcb_in(fcb *dir_ptr, fcb *fcb_ptr);

static void rewrite_data(fcb *tar_fcb_ptr, char data[], size_t n);

static void get_dir(fcb *dir_ptr, fcb dir[], size_t *dir_size_ptr);

/**
 * 初始化，读取数据文件到内存，如果数据文件存在则正常读取，不存在则先初始化内存的各种上下文信息，等程序退出时会自动创建数据文件并按照预定结构写入
 */
void start_sys(void) {
    FILE *data_file;
    size_t read;

    // 分配虚拟磁盘空间
    dist = (char *) malloc(DIST_SIZE * sizeof(char));
    if (dist == NULL) {
        perror("Dist malloc error!");
        exit(EXIT_FAILURE);
    }

    // 打开实际磁盘文件
    data_file = fopen(REAL_DATA_FILE, "rb");
    if (data_file == NULL) { // 还未初始化，需要初始化一下
        // 初始化 BLOCK0
        blk0.data_start = DATA_START;
        strcpy(blk0.root_dir_fcb.filename, "/");
        blk0.root_dir_fcb.is_file = 0;
        time(&(blk0.root_dir_fcb.created_time));
        blk0.root_dir_fcb.len = 0;
        blk0.root_dir_fcb.first = ROOT_DIR_FIRST;
        // 在虚拟磁盘中维护 BLOCK0
        memcpy(dist, &blk0, sizeof(block0));

        // 初始化 FAT
        fat[0] = END;
        fat[FAT_START] = FAT_START + 1;
        fat[FAT_START + 1] = END;
        fat[ROOT_DIR_FIRST] = END;
        for (int i = ROOT_DIR_FIRST + 1; i < BLOCK_ASSET; i++) {
            fat[i] = FREE;
        }
        // 在虚拟磁盘中维护 FAT
        memcpy(dist + FAT_START * BLOCK_SIZE, fat, sizeof(fat));

        // 初始化 fcb_stack
        fcb_stack[fcb_stack_size++] = blk0.root_dir_fcb;
    } else {
        // 读取实际磁盘文件到虚拟磁盘空间
        for (int i = 0; i < BLOCK_ASSET; i++) {
            if (BLOCK_SIZE != fread(dist + i * BLOCK_SIZE, sizeof(char), BLOCK_SIZE, data_file)) {
                perror("Data file read error!");
                free(dist);
                if (fclose(data_file)) perror("Data file close error!");
                exit(EXIT_FAILURE);
            }
        }

        // 关闭实际磁盘文件
        if (fclose(data_file)) {
            perror("Data file close error!");
            free(dist);
            exit(EXIT_FAILURE);
        }

        // 初始化 BLOCK0，位置在第一个盘块
        memcpy(&blk0, dist, sizeof(block0));

        // 初始化 FAT
        memcpy(fat, dist + FAT_START * BLOCK_SIZE, sizeof(fat));

        // 初始化 fcb_stack
        fcb_stack[fcb_stack_size++] = blk0.root_dir_fcb;

        // 初始化当前目录
        read = 0;
        for (int i = blk0.root_dir_fcb.first; i != END; i = fat[i]) {
            size_t n = MIN(BLOCK_SIZE, blk0.root_dir_fcb.len - read);
            memcpy(cur_dir + read, dist + i * BLOCK_SIZE, n);
            read += n;
        }
        cur_dir_size = (int) (blk0.root_dir_fcb.len / sizeof(fcb));
    }
}

/**
 * 循环读取从控制台输入的一行命令，不支持换行，只能一行
 */
void command() {
    int i;
    int j;
    size_t len;

    while (1) {
        cmd_args_size = 0; // 重置

        print_path(); // 打印命令行前段路径

        fgets(cmd_arg, sizeof(cmd_arg), stdin); // 读取命令
        len = strlen(cmd_arg);
        if (len > 0 && cmd_arg[len - 1] == '\n') cmd_arg[--len] = '\0'; // fgets 函数以换行符为结尾，不以空格符，所以需要补上结束符 '\0'

        // 解析命令，按空格（可能是连续空格）分隔
        i = 0;
        j = 0;
        while (1) {
            if (i == len) { // 读取完了
                if (j != 0) cmd_args[cmd_args_size++][j] = '\0';
                break;
            }

            if (cmd_arg[i] != ' ') cmd_args[cmd_args_size][j++] = cmd_arg[i]; // 读到空格以外的字符
            else if (j != 0) { // 读到空格字符 && 上一个是普通字符
                cmd_args[cmd_args_size++][j] = '\0';
                j = 0;
            }
            // 如果以上两种情况都不是，即 读到空格字符 && (上一个也是空格 || 当前是第一个字符)，不需要做额外的事，正常 i++ 即可

            i++;
        }

        if (cmd_args_size == 0) continue; // 输入全是空格 或 只输入了回车

        // 此时至少有一个命令

        if (!strcmp(MY_EXITSYS, cmd_args[0])) { // 退出命令
            if (cmd_args_size > 1) printf("Unknown command: %s\n", cmd_arg);
            else {
                sys_exit();
                break;
            }
        } else if (!strcmp(MY_LS, cmd_args[0])) my_ls();
        else if (!strcmp(MY_FORMAT, cmd_args[0])) my_format();
        else if (!strcmp(MY_CD, cmd_args[0])) my_cd();
        else if (!strcmp(MY_MKDIR, cmd_args[0])) my_mkdir();
        else if (!strcmp(MY_RMDIR, cmd_args[0])) my_rmdir();
        else if (!strcmp(MY_CREATE, cmd_args[0])) my_create();
        else if (!strcmp(MY_RM, cmd_args[0])) my_rm();
        else printf("Unknown command: %s\n", cmd_arg);
    }
}

/**
 * 退出当前系统需要完成的收尾操作
 */
static void sys_exit(void) {
    // 刷新 blk0 到虚拟磁盘
    memcpy(dist, &blk0, sizeof(blk0));

    // 刷新 fat 到虚拟磁盘
    memcpy(dist + FAT_START * BLOCK_SIZE, fat, sizeof(fat));

    persistence(); // 虚拟磁盘持久化
    free(dist); // 释放分配内存
}

/**
 * 遵循传统命令行格式，打印当前路径 + "# "，如 "/folder1/folder2# "
 */
static void print_path(void) {
    char path[64];
    size_t path_size = 0;
    // 遍历一遍 fcb_stack 即可
    for (int i = 0; i < fcb_stack_size; ++i) {
        for (int j = 0; fcb_stack[i].filename[j] != '\0'; j++) {
            path[path_size++] = fcb_stack[i].filename[j];
        }
    }
    path[path_size] = '\0';
    printf("%s# ", path);
}

/**
 * 持久化虚拟磁盘数据
 */
static void persistence(void) {
    // 以写入二进制形式打开数据文件
    FILE *data_file = fopen(REAL_DATA_FILE, "wb");
    if (data_file == NULL) {
        perror("Data file close error!");
        free(dist);
        exit(EXIT_FAILURE);
    }

    // 分块写入磁盘文件
    for (int i = 0; i < BLOCK_ASSET; ++i) {
        if (BLOCK_SIZE != fwrite(dist + i * BLOCK_SIZE, sizeof(char), BLOCK_SIZE, data_file)) {
            perror("Data file write error!");
            free(dist);
            if (fclose(data_file)) perror("Data file close error!");
            exit(EXIT_FAILURE);
        }
    }

    // 写入完成，关闭数据文件
    if (fclose(data_file)) {
        perror("Data file close error!");
        free(dist);
        exit(EXIT_FAILURE);
    }
}

/**
 * 列出当前目录
 * "my_ls"：列出简单目录
 * "my_ls -a"：列出目录时，包含详细信息
 */
static void my_ls() {
    if (cmd_args_size > 2) { // 参数长度校验
        printf("Unknown command: %s\n", cmd_arg);
        return;
    }

    if (cmd_args_size == 1) { // 列出简单目录
        // 遍历当前目录
        for (int i = 0; i < cur_dir_size; ++i) {
            // 一行打印5个
            if (i % 5 == 0) printf("\n");

            char name[24];
            sprintf(name, "%s%s", cur_dir[i].filename, cur_dir[i].ext);
            printf("%-32s", name);
        }
    } else {
        // 带 "-a" 参数，表示列出目录时，需包含详细信息
        if (strcmp(cmd_args[1], "-a") != 0) {
            printf("Unknown command: %s\n", cmd_arg);
            return;
        }

        char *format = "%-32s%-32s%-32s\n";

        // 表头
        printf(format, "name", "size", "created_time");

        // 遍历当前目录
        for (int i = 0; i < cur_dir_size; ++i) {
            // 文件名或目录名
            char name[24];
            sprintf(name, "%s%s", cur_dir[i].filename, cur_dir[i].ext);

            // 文件大小正常显示，目录大小显示 "/"
            char size[32];
            if (cur_dir[i].is_file) sprintf(size, "%d", cur_dir[i].len);
            else strcpy(size, "/");

            // 创建时间
            char time[32];
            strftime(time, sizeof(time), "%Y-%m-%d %H:%M:%S", localtime(&(cur_dir[i].created_time)));

            printf(format, name, size, time);
        }
    }
}

/**
 * 格式化文件系统
 */
static void my_format() {
    if (cmd_args_size > 2) { // 参数长度校验
        printf("Unknown command: %s\n", cmd_arg);
        return;
    }

    // blk0
    time(&(blk0.root_dir_fcb.created_time));
    blk0.root_dir_fcb.len = 0;

    // fat
    fat[ROOT_DIR_FIRST] = END;
    for (int i = ROOT_DIR_FIRST + 1; i < BLOCK_ASSET; i++) fat[i] = FREE;

    // 当前目录 cur_dir
    cur_dir[cur_dir_size = 0] = blk0.root_dir_fcb;

    // FCB 栈
    fcb_stack[fcb_stack_size = 0] = blk0.root_dir_fcb;

    printf("Done\n");
}

/**
 * 改变当前路径至任意存在的目录
 */
static void my_cd() {
    if (cmd_args_size != 2) { // 参数长度校验
        printf("Unknown command: %s\n", cmd_arg);
        return;
    }

    // 用户输入的路径，"a/b/c" --> ["a", "b", "c"]
    char paths[16][16];
    size_t paths_size = 0;

    // 临时文件目录
    fcb tmp_cur_dir[20];
    size_t tmp_cur_dir_size = 0;

    // 临时 FCB 目录层级栈
    fcb tmp_fcb_stack[20];
    size_t tmp_fcb_stack_size = 0;

    int i;

    // 解析路径参数
    if (parse_path(cmd_args[1], paths, &paths_size)) {
        printf("%s: No such directory", cmd_args[1]);
        return;
    }

    // 如果解析后的路径为空，直接返回
    if (paths_size == 0) return;

    if (!strcmp(paths[0], "/")) { // 绝对路径，以 "/" 起始的路径
        i = 1;

        // 读取根目录的内容
        get_data_from_dist(tmp_cur_dir, blk0.root_dir_fcb.first, blk0.root_dir_fcb.len);
        tmp_cur_dir_size = blk0.root_dir_fcb.len / sizeof(fcb);

        // 根目录 FCB 入栈
        tmp_fcb_stack[tmp_fcb_stack_size++] = blk0.root_dir_fcb;
    } else { // 相对路径
        i = 0;

        // 将当前目录信息拷贝一份
        memcpy(tmp_cur_dir, cur_dir, sizeof(cur_dir));
        tmp_cur_dir_size = cur_dir_size;

        // 将当前 FCB 栈信息拷贝一份
        memcpy(tmp_fcb_stack, fcb_stack, sizeof(fcb_stack));
        tmp_fcb_stack_size = fcb_stack_size;
    }

    // 遍历用户输入的每一段路径
    for (; i < paths_size; ++i) {
        // 遇到 "." 则可以直接跳过
        if (!strcmp(paths[i], ".")) continue;

        fcb tar_fcb;
        if (!strcmp(paths[i], "..")) { // 返回上一级目录
            if (tmp_fcb_stack_size == 1) { // 当前在根目录，没有上一级目录了，直接报错
                printf("%s: No such directory", cmd_args[1]);
                return;
            }

            tmp_fcb_stack_size--; // 出栈
            tar_fcb = tmp_fcb_stack[tmp_fcb_stack_size - 1];
        } else { // 进入下一级目录
            // 不存在直接打印错误信息并退出
            if (get_fcb_from(&tmp_fcb_stack[tmp_fcb_stack_size - 1], paths[i], 0, &tar_fcb)) {
                printf("%s: No such directory", cmd_args[1]);
                return;
            }

            tmp_fcb_stack[tmp_fcb_stack_size++] = tar_fcb; // 找到目标目录后将 FCB 入栈
        }

        // 读取文件目录
        get_data_from_dist(tmp_cur_dir, tar_fcb.first, tar_fcb.len);
        tmp_cur_dir_size = tar_fcb.len / sizeof(fcb);
    }

    // 这时已经找到了最终目标目录
    // 维护全局变量 cur_dir, fcb_stack
    memcpy(cur_dir, tmp_cur_dir, sizeof(tmp_cur_dir));
    cur_dir_size = tmp_cur_dir_size;
    memcpy(fcb_stack, tmp_fcb_stack, sizeof(tmp_fcb_stack));
    fcb_stack_size = tmp_fcb_stack_size;
}

/**
 * 创建文件夹，路径段不能含有 ".." 和 "."。
 */
static void my_mkdir() {
    if (cmd_args_size != 2) { // 参数长度校验
        printf("Unknown command: %s\n", cmd_arg);
        return;
    }

    // 解析路径
    char paths[16][16];
    size_t paths_size = 0;
    if (parse_path(cmd_args[1], paths, &paths_size)) {
        printf("Unknown command: %s\n", cmd_arg);
        return;
    }
    if (paths_size == 0) return; // 如果解析后的路径为空，直接返回

    // 校验合法性
    for (int i = 0; i < paths_size; ++i) {
        if (!strcmp(paths[i], "..") || !strcmp(paths[i], ".")) {
            printf("%s: Path can't contain \".\" or \"..\"\n", cmd_arg);
            return;
        }
    }

    fcb cur_fcb; // 当前目录的 FCB
    int i = 0;
    if (!strcmp(paths[0], "/")) { // 绝对路径
        i = 1;
        cur_fcb = blk0.root_dir_fcb;
    } else { // 相对路径
        i = 0;
        cur_fcb = fcb_stack[fcb_stack_size - 1];
    }

    // 遍历路径段
    for (; i < paths_size; i++) {
        fcb tar_fcb;

        // 不存在目录，需要创建
        if (get_fcb_from(&cur_fcb, paths[i], 0, &tar_fcb))
            create_fcb_in(&cur_fcb, paths[i], &tar_fcb, 0);

        cur_fcb = tar_fcb;
    }

    printf("%s: Create directory success\n", cmd_args[1]);
}

/**
 * 删除指定目录。
 * 路径不能包含 "." 和 ".."
 */
static void my_rmdir() {
    if (cmd_args_size != 2) { // 参数长度校验
        printf("Unknown command: %s\n", cmd_arg);
        return;
    }

    // 解析路径
    char paths[16][16];
    size_t paths_size = 0;
    if (parse_path(cmd_args[1], paths, &paths_size)) {
        printf("Unknown command: %s\n", cmd_arg);
        return;
    }
    if (paths_size == 0) return; // 如果解析后的路径为空，直接返回

    // 校验合法性，路径不能包含 "." 和 ".."
    for (int i = 0; i < paths_size; ++i) {
        if (!strcmp(paths[i], "..") || !strcmp(paths[i], ".")) {
            printf("%s: Path can't contain \".\" or \"..\"\n", cmd_arg);
            return;
        }
    }

    // 校验当前位置是否是目标删除目录或在目标删除目录中
    for (int i = 0, j = 0;; i++, j++) {
        if (j == paths_size) {
            printf("%s: Can't remove directory where you in", cmd_arg);
            return;
        }

        if (i == fcb_stack_size || strcmp(fcb_stack[i].filename, paths[j]) != 0) break;
    }

    fcb prev_fcb; // 上一级目录的 FCB
    fcb cur_fcb; // 当前目录的 FCB
    int k = 0;
    if (!strcmp(paths[0], "/")) { // 绝对路径
        k = 1;
        cur_fcb = blk0.root_dir_fcb;
    } else { // 相对路径
        k = 0;
        cur_fcb = fcb_stack[fcb_stack_size - 1];
        if (fcb_stack_size > 1) prev_fcb = fcb_stack[fcb_stack_size - 2];
    }

    // 遍历 paths，将 cur_fcb 移动到目标删除目录，同时维护 prev_fcb
    for (; k < paths_size; k++) {
        fcb tar_fcb;

        // 不存在目录，返回错误
        if (get_fcb_from(&cur_fcb, paths[k], 0, &tar_fcb)) {
            printf("%s: No such directory\n", cmd_arg);
            return;
        }

        prev_fcb = cur_fcb;
        cur_fcb = tar_fcb;
    }

    // 删除目录
    rmfcb_in(&prev_fcb, &cur_fcb);
}

/**
 * 创建文件，路径段不能含有 ".." 和 "."。
 */
static void my_create() {
    if (cmd_args_size != 2) { // 参数长度校验
        printf("Unknown command: %s\n", cmd_arg);
        return;
    }

    // 解析路径
    char paths[16][16];
    size_t paths_size = 0;
    if (parse_path(cmd_args[1], paths, &paths_size)) {
        printf("Unknown command: %s\n", cmd_arg);
        return;
    }
    if (paths_size == 0) return; // 如果解析后的路径为空，直接返回

    // 校验是否含有 "." 和 ".."
    for (int i = 0; i < paths_size; ++i) {
        if (!strcmp(paths[i], "..") || !strcmp(paths[i], ".")) {
            printf("%s: Path can't contain \".\" or \"..\"\n", cmd_arg);
            return;
        }
    }

    fcb cur_fcb; // 当前目录的 FCB
    int i = 0;
    if (!strcmp(paths[0], "/")) { // 绝对路径
        i = 1;
        cur_fcb = blk0.root_dir_fcb;
    } else { // 相对路径
        i = 0;
        cur_fcb = fcb_stack[fcb_stack_size - 1];
    }

    // 遍历路径段
    while (1) {
        fcb tar_fcb;

        // 最后一个路径段为文件名
        if (i == paths_size - 1) {
            if (!get_fcb_from(&cur_fcb, paths[i], 1, &tar_fcb)) {
                printf("%s: File already exist", cmd_arg);
                return;
            }

            create_fcb_in(&cur_fcb, paths[i], &tar_fcb, 1);
            break;
        }

        // 不存在目录，需要创建
        if (get_fcb_from(&cur_fcb, paths[i], 0, &tar_fcb))
            create_fcb_in(&cur_fcb, paths[i], &tar_fcb, 0);

        cur_fcb = tar_fcb;
    }

    printf("%s: File created", cmd_arg);
}

/**
 * 删除文件，路径不能包含 "." 和 ".."
 */
static void my_rm() {
    if (cmd_args_size != 2) { // 参数长度校验
        printf("Unknown command: %s\n", cmd_arg);
        return;
    }

    // 解析路径
    char paths[16][16];
    size_t paths_size = 0;
    if (parse_path(cmd_args[1], paths, &paths_size)) {
        printf("Unknown command: %s\n", cmd_arg);
        return;
    }
    if (paths_size == 0) return; // 如果解析后的路径为空，直接返回

    // 校验是否含有 "." 和 ".."
    for (int i = 0; i < paths_size; ++i) {
        if (!strcmp(paths[i], "..") || !strcmp(paths[i], ".")) {
            printf("%s: Path can't contain \".\" or \"..\"\n", cmd_arg);
            return;
        }
    }

    fcb prev_fcb; // 上一级目录的 FCB
    fcb cur_fcb; // 当前目录的 FCB
    int i = 0;
    if (!strcmp(paths[0], "/")) { // 绝对路径
        i = 1;
        cur_fcb = blk0.root_dir_fcb;
    } else { // 相对路径
        i = 0;
        cur_fcb = fcb_stack[fcb_stack_size - 1];
        if (fcb_stack_size > 1) prev_fcb = fcb_stack[fcb_stack_size - 2];
    }

    // 遍历 paths，将 cur_fcb 移动到目标文件，同时维护 prev_fcb
    while (1) {
        fcb tar_fcb;

        // 最后一个路径段为文件名
        if (i == paths_size - 1) {
            // 如果不存在目标文件
            if (get_fcb_from(&cur_fcb, paths[i], 1, &tar_fcb)) {
                printf("%s: No such file", cmd_arg);
                return;
            }

            rmfcb_in(&prev_fcb, &cur_fcb);
            break;
        }

        // 不存在目录，返回错误
        if (get_fcb_from(&cur_fcb, paths[i], 0, &tar_fcb)) {
            printf("%s: No such file", cmd_arg);
            return;
        }

        prev_fcb = cur_fcb;
        cur_fcb = tar_fcb;
    }

    printf("%s: File created", cmd_arg);
}

/**
 * 解析路径字符串为路径段数组，会校验格式是否正确，但不会校验路径是否真实存在。</br>
 * "/a/b" --> ["/", "a", "b"]</br>
 * "a/b" --> ["a", "b"]</br>
 * "./a/b/c/.././.." --> [".", "a", "b", "c", "..", ".", ".."]</br>
 * "/a/b/" --> 格式错误</br>
 * "a/b/" --> 格式错误</br>
 * "/.." --> ["/", ".."]
 * @param src 整个路径
 * @param dest 接收缓冲区
 * @param dest_size_ptr 数组长度接收缓冲区
 * @return 0：格式正确；1：格式错误
 */
static int parse_path(const char src[16], char dest[16][16], size_t *dest_size_ptr) {
    char tmp_dest[16][16];
    size_t tmp_dest_size = 0;

    // 解析路径为多个路径段
    // flag 表示上一个字符是否为 '/'
    for (int i = 0, j = 0, flag = 0;;) {
        if (src[i] == '\0') { // 读完了
            if (j != 0) tmp_dest[tmp_dest_size++][j] = '\0'; // 正常结束
            else if (i > 1) return 1; // 以 '/' 结尾的情况，其中只有 ""(空串) 和 "/" 合法，其他都属于格式错误，如 "a/" "a/b/"

            break;
        }

        if (src[i] == '/') {
            if (i == 0) tmp_dest[tmp_dest_size][j++] = '/'; // 以 '/' 开头，属于绝对路径
            else if (flag) return 1; // 一旦遇到连续的“/”，直接返回格式错误

            tmp_dest[tmp_dest_size++][j] = '\0';
            j = 0;
            flag = 1;
        } else {
            tmp_dest[tmp_dest_size][j++] = src[i];
            flag = 0;
        }

        i++;
    }

//    // 优化，缩短路径长度，如 "./a/b/../.././.." --> ".."
//    for (int k = 0; k < tmp_dest_size; ++k) {
//        // 如果是 "." 直接跳过
//        if (!strcmp(tmp_dest[k], ".")) continue;
//
//        // 如果不是 ".." 直接加入
//        // 如果是 ".." 并且当前路径段长度为 0 或上一个也是 ".." 或上一个是根路径 "/"，直接加入
//        // 否则回退一个路径段
//        if (strcmp(tmp_dest[k], "..") != 0) strcpy(dest[(*dest_size_ptr)++], tmp_dest[k]);
//        else if (*dest_size_ptr == 0 || !strcmp(dest[(*dest_size_ptr) - 1], "..") ||
//                 !strcmp(dest[(*dest_size_ptr) - 1], "/"))
//            strcpy(dest[(*dest_size_ptr)++], "..");
//        else (*dest_size_ptr)--;
//    }

    memcpy(dest, tmp_dest, sizeof(tmp_dest));
    *dest_size_ptr = tmp_dest_size;

    return 0;
}

/**
 * 从虚拟磁盘中读取数据
 * @param dest 接收缓冲区
 * @param first_block 第一个磁盘块
 * @param n 要读取的字节数
 */
static void get_data_from_dist(void *dest, unsigned short first_block, size_t n) {
    size_t offset = 0;
    unsigned short cur_block = first_block;

    while (n - offset > 0) {
        size_t to_read = MIN(BLOCK_SIZE, n - offset);
        memcpy(dest + offset, dist + cur_block * BLOCK_SIZE, to_read);

        offset += to_read;
        cur_block = fat[cur_block];
    }
}

/**
 * 在指定目录中寻找目标文件或目录
 * @param dir_fcb_ptr 指定目录的 FCB
 * @param filename 文件名/目录名
 * @param is_file 是否为文件
 * @param fcb_ptr 接收缓冲区
 * @return 返回0：目标存在；返回1：不存在
 */
static int get_fcb_from(fcb *dir_fcb_ptr, char filename[16], unsigned char is_file, fcb *fcb_ptr) {
    fcb dir[20];
    get_data_from_dist(dir, dir_fcb_ptr->first, dir_fcb_ptr->len);
    size_t dir_len = dir_fcb_ptr->len / sizeof(fcb);

    int res = 0; // 是否存在
    for (int i = 0; i < dir_len; i++) {
        if (dir[i].is_file == is_file && !strcmp(dir[i].filename, filename)) {
            *fcb_ptr = dir[i];
            res = 1;
            break;
        }
    }
    return res;
}

/**
 * 从指定位置开始寻找第一个空闲盘块
 * @param from 指定开始盘块
 * @return 小于 BLOCK_ASSET 的值：下一个空闲盘块；大于等于 BLOCK_ASSET 的值：磁盘已满，找不到空闲块
 */
static unsigned short next_free_block(void) {
    for (int i = blk0.data_start; i < BLOCK_ASSET; i++)
        if (fat[i] == FREE) return i;
    return BLOCK_ASSET;
}

/**
 * 在指定目录下创建空目录或空文件
 * @param dir_fcb_ptr 目标目录的 FCB
 * @param name 要创建目录或文件（包括扩展名）的名称
 * @param fcb_ptr 新 FCB 的接收缓冲区
 * @param is_file 创建目录还是创建文件
 * @return 0：成功创建；1：当前目录下有重名
 */
static int create_fcb_in(fcb *dir_fcb_ptr, char *name, fcb *fcb_ptr, unsigned char is_file) {
    // 截取不包含扩展名的名称
    char filename[16];
    size_t filename_size = 0;
    for (int i = 0;; i++) {
        if (name[i] == '\0' || name[i] == '.') {
            filename[filename_size++] = '\0';
            break;
        }
        filename[filename_size++] = name[i];
    }

    // 判断是否有重名
    fcb dir[20];
    get_data_from_dist(dir, dir_fcb_ptr->first, dir_fcb_ptr->len);
    size_t dir_size = dir_fcb_ptr->len / sizeof(fcb);
    for (int i = 0; i < dir_size; ++i) {
        if (!strcmp(dir[i].filename, filename)) { // 重名了
            return 1;
        }
    }

    // 创建 FCB
    fcb new_dir;
    strcpy(new_dir.filename, filename);
    strcpy(new_dir.ext, name + filename_size);
    new_dir.is_file = is_file;
    time(&(new_dir.created_time));
    new_dir.len = 0;
    fat[new_dir.first = next_free_block()] = END;

    // 插入到目录中
    dir[dir_size++] = new_dir;

    // 重新写回虚拟磁盘
    char buf[1024];
    memcpy(buf, dir, dir_size * sizeof(fcb));
    size_t buf_size = dir_size * sizeof(fcb);
    rewrite_data(dir_fcb_ptr, buf, buf_size);

    // 赋值到缓冲区
    *fcb_ptr = new_dir;

    return 0;
}

/**
 * 在指定目录中删除目录或文件
 * @param dir_ptr 指定目录
 * @param fcb_ptr 目标 FCB，可能是目录，可能是文件
 */
static void rmfcb_in(fcb *dir_ptr, fcb *fcb_ptr) {
    // 获取指定目录
    fcb dir[20];
    size_t dir_size;
    get_dir(dir_ptr, dir, &dir_size);

    // 找目标 FCB 在当前目录下的位置
    int i = 0;
    while (i < dir_size && strcmp(dir[i].filename, fcb_ptr->filename) != 0) i++;
    if (i == dir_size) return;

    // 分情况删除
    if (fcb_ptr->is_file) { // 目标删除 FCB 是文件
        unsigned short cur_block = fcb_ptr->first;
        while (1) {
            if (fat[cur_block] == END) {
                fat[cur_block] = FREE;
                break;
            }

            unsigned short next = fat[cur_block];
            fat[cur_block] = FREE;
            cur_block = next;
        }
    } else { // 目标删除 FCB 是目录，则递归删除
        // 获取要删除目录的文件目录
        fcb tar_dir[20];
        size_t tar_dir_size;
        get_dir(fcb_ptr, tar_dir, &tar_dir_size);

        // 遍历，递归删除
        for (int k = 0; k < tar_dir_size; ++k) {
            rmfcb_in(fcb_ptr, &tar_dir[k]);
        }

        // 回收 first_block
        fat[fcb_ptr->first] = FREE;
    }

    // 在当前目录中移除目标删除 FCB
    while (i + 1 < dir_size) {
        dir[i] = dir[i + 1];
        i++;
    }
    dir_size = i;

    // 将当前目录重新写入虚拟磁盘空间
    char buf[1024];
    memcpy(buf, dir, dir_size * sizeof(fcb));
    size_t buf_size = dir_size * sizeof(fcb);
    rewrite_data(dir_ptr, buf, buf_size);
}

/**
 * 将数据重新写回目标 FCB 的虚拟磁盘
 * @param tar_fcb_ptr 目标 FCB
 * @param data 字节数据
 * @param n 字节数
 */
static void rewrite_data(fcb *tar_fcb_ptr, char data[], size_t n) {
    size_t data_offset = 0;
    size_t block_offset = 0;
    unsigned short cur_block = tar_fcb_ptr->first;
    while (n - data_offset > 0) {
        size_t to_write = MIN(n - data_offset, BLOCK_SIZE - block_offset);
        memcpy(dist + cur_block * BLOCK_SIZE, data + data_offset, to_write);

        data_offset += to_write;
        block_offset += to_write;
        if (block_offset == BLOCK_SIZE) { // 当前块写满了
            cur_block = (fat[cur_block] == END)
                        ? fat[cur_block] = next_free_block()
                        : fat[cur_block];
        }
    }

    // 数据可能变少了，需要释放磁盘块
    if (fat[cur_block] != END) {
        unsigned short clean_first = fat[cur_block];
        fat[cur_block] = END;

        while (1) {
            if (fat[clean_first] == END) {
                fat[clean_first] = FREE;
                break;
            }

            unsigned short next = fat[clean_first];
            fat[clean_first] = FREE;
            clean_first = next;
        }
    }

    // 维护 FCB 的 len 字段
    tar_fcb_ptr->len = n;
}

/**
 * 获取指定目录
 * @param dir_ptr 目标目录 FCB
 * @param dir 目录接收缓冲区
 * @param dir_size_ptr 目录长度接收缓冲区
 */
static void get_dir(fcb *dir_ptr, fcb dir[], size_t *dir_size_ptr) {
    // 获取指定目录
    get_data_from_dist(dir, dir_ptr->first, dir_ptr->len);
    *dir_size_ptr = dir_ptr->len / sizeof(fcb);
}
