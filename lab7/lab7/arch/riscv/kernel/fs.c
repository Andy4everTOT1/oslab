#include "fs.h"
#include "buf.h"
#include "defs.h"
#include "slub.h"
#include "task_manager.h"
#include "virtio.h"
#include "vm.h"
#include "mm.h"

// --------------------------------------------------
// ----------- read and write interface -------------

void disk_op(int blockno, uint8_t *data, bool write) {
    struct buf b;
    b.disk = 0;
    b.blockno = blockno;
    b.data = (uint8_t *)PHYSICAL_ADDR(data);
    virtio_disk_rw((struct buf *)(PHYSICAL_ADDR(&b)), write);
}

#define disk_read(blockno, data) disk_op((blockno), (data), 0)
#define disk_write(blockno, data) disk_op((blockno), (data), 1)

// -------------------------------------------------
// ------------------ your code --------------------
// 全局变量
struct sfs_fs SFS;
bool sfsInited = 0; // 保存是否初始化过

void strcpy(char *dst, const char *src) {
    for (; *src; *dst++ = *src++)
        ;
    *dst = '\0';
}
int strlen(const char *a) {
    int count;
    for (count = 0; a[count]; count++)
        ;
    return count;
}
uint32_t min(uint32_t x, uint32_t y) {
    return x < y ? x : y;
}
uint32_t hash(uint32_t x) {
    return x % BUFFER_SIZE;
}

struct list_entry_t *cacheGet(uint32_t blockno) {
    for (struct list_entry_t *node = SFS.block_list[hash(blockno)]; node != NULL; node = node->next)
        if (blockno == node->data->blockno)
            return node;
    return NULL;
}
struct list_entry_t *cacheNew(uint32_t blockno) {
    uint32_t hashID = hash(blockno);
    // 先在缓存池中查找
    struct list_entry_t *node;
    for (node = SFS.block_list[hashID]; node != NULL; node = node->next)
        if (blockno == node->data->blockno)
            break;
    // 如果找不到就创建新的节点
    if (node == NULL) {
        // 初始化新块
        node = kmalloc(sizeof(struct list_entry_t));
        node->data = kmalloc(sizeof(struct sfs_memory_block));
        node->data->block = kmalloc(BLOCK_SIZE);
        node->data->blockno = blockno;
        node->data->dirty = 0;
        node->data->reclaim_count = 0;
        node->data->inode_link = node;
        disk_read(blockno, node->data->block);
        // 插入节点到表头
        struct list_entry_t *nextnode = SFS.block_list[hashID];
        SFS.block_list[hashID] = node;
        node->prev = NULL;
        node->next = nextnode;
        if (nextnode) {
            nextnode->prev = node;
        }
    }
    return node;
}
void cacheDelete(uint32_t blockno) {
    uint32_t hashID = hash(blockno);
    // 先在缓存池中查找
    struct list_entry_t *node;
    for (node = SFS.block_list[hashID]; node != NULL; node = node->next)
        if (blockno == node->data->blockno)
            break;
    if (node == NULL)
        return;
    // 如果找到就删除节点
    struct list_entry_t *nextnode = node->next;
    struct list_entry_t *prevnode = node->prev;
    // 更新前后节点
    if (prevnode)
        prevnode->next = nextnode;
    if (nextnode)
        nextnode->prev = prevnode;
    // 更新表头
    if (node == SFS.block_list[hashID]) {
        SFS.block_list[hashID] = nextnode;
    }
    kfree(node->data->block);
    kfree(node->data);
    kfree(node);
}

// 获取一个block
void *readBlock(uint32_t blockno) {
    struct list_entry_t *node = cacheNew(blockno);
    node->data->reclaim_count += 1;
    return node->data->block;
}
// 设置dirty
void modifyBlock(uint32_t blockno) {
    struct list_entry_t *node = cacheGet(blockno);
    if (node == NULL) {
        printf("modifyBlock failed: block not found\n");
        return;
    }
    node->data->dirty = 1;
}
// 释放一个块
void releaseBlock(uint32_t blockno) {
    struct list_entry_t *node = cacheGet(blockno);
    // // 如果找不到就直接返回
    // if (node == NULL)
    //     return;
    node->data->reclaim_count--;
    // 如果没有指针引用就可以释放
    if (node->data->reclaim_count == 0) {
        printf("[releaseBlock]write back %d\n", blockno);
        if (node->data->dirty) {
            node->data->dirty = 0;
            disk_write(blockno, node->data->block);
        }
        printf("[releaseBlock]release %d\n", blockno);
        cacheDelete(blockno);
    }
}
// 尝试写回某个块
void writeBlock(uint32_t blockno) {
    printf("[writeBlock]write back %d\n", blockno);
    struct list_entry_t *node = cacheGet(blockno);
    // 如果找不到就直接返回
    if (node == NULL)
        return;
    // 脏块写回
    if (node->data->dirty) {
        node->data->dirty = 0;
        disk_write(blockno, node->data->block);
    }
}
// 找到一个新的空Block的编号
uint32_t NewBlock() { // 暂时不考虑磁盘空间不够的情况
    SFS.super.unused_blocks--;
    SFS.super_dirty = 1;
    int i = 0, j = 0;
    for (; i <= BLOCK_SIZE && SFS.freemap->freemap[i] == 0xFF; i++)
        ; // 找到第一个可能空的块
    for (; (SFS.freemap->freemap[i] >> j) & 1; j++)
        ;                              // 找到空块里的第一个空点
    SFS.freemap->freemap[i] |= 1 << j; // 初始化
    return (i << 3) + j;
}
// 将一个inode的所有数据块写回
void writeInode(uint32_t blockno, bool Type) {
    struct sfs_inode *node = cacheGet(blockno);
    // // 如果找不到就直接返回
    // if (node == NULL)
    //     return;
    printf("[writeInode]no: %d, size: %d\n", blockno, node->size);
    int numBlock = (node->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    // 直接索引
    for (int i = 0; i < SFS_NDIRECT && i < numBlock; i++) {
        writeBlock(node->direct[i]);
        // releaseBlock(node->direct[i]);
    }
    // 间接索引
    if (numBlock > SFS_NDIRECT) {
        uint32_t *indirect = readBlock(node->indirect);
        for (int i = SFS_NDIRECT; i < numBlock; i++) {
            writeBlock(indirect[i - SFS_NDIRECT]);
            // releaseBlock(indirect[i - SFS_NDIRECT]);
        }
        writeBlock(node->indirect);
        // releaseBlock(node->indirect);
    }
}

// looks like there is a bug in the memory system
void memory_bug() {
    struct list_entry_t *node;
    for (int count = 0; count < 1 << 12; count++) {
        node = kmalloc(sizeof(struct list_entry_t));
        node->data = kmalloc(sizeof(struct sfs_memory_block));
        node->data->block = kmalloc(BLOCK_SIZE);
        kfree(node->data->block);
        kfree(node->data);
        kfree(node);
    }
}

// -------------------------------------------------

int sfs_init() {                // 应该直接读取块中的内容比较好
    disk_read(0, (&SFS.super)); // 读取内容
    SFS.freemap = (struct bitmap *)kmalloc(sizeof(struct bitmap));
    if (SFS.freemap == NULL)
        return -1;
    disk_read(2, SFS.freemap->freemap); // 读取freemap
    SFS.super_dirty = 0;                // 刚开始并没有修改过超级块或者freemap
    memset(SFS.block_list, NULL, sizeof(SFS.block_list));
    sfsInited = 1;
    printf("[sfs_init]init success!\n");
    return 0;
}

/**
 * 功能: 打开一个文件, 读权限下如果找不到文件，则返回一个小于 0
 * 的值，表示出错，写权限如果没有找到文件，则创建该文件（包括缺失路径）
 * @path : 文件路径 (绝对路径)
 * @flags: 读写权限 (read, write, read | write)
 * @ret  : file descriptor (fd), 每个进程根据 fd 来唯一的定位到其一个打开的文件
 *         正常返回一个大于 0 的 fd 值, 其他情况表示出错
 */
int sfs_open(const char *path, uint32_t flags) {
    // 检查是否初始化
    if (!sfsInited && sfs_init())
        return -1;
    printf("[sfs_open]path: %s\n", path);
    // 路径必须从根目录开始
    if (path[0] != '/')
        return -1;

    struct sfs_inode *node = readBlock(1), *preNode = NULL,
                     *nextNode = NULL; // PreNode存储上一个节点
    int nameStart = 1, name_end = 1;
    uint32_t ino = 1, preino = 1, nextino = 1;
    while (path[name_end]) {
        if (path[nameStart] == '/')
            return -1; // /连用，无法识别
        else if (path[nameStart] == 0)
            break; // 合法情况，如/home/
        // 如果上层目录是文件
        if (node->type == SFS_FILE)
            return -1;
        // 找到下一个'/'的位置
        for (name_end += 1; path[name_end] && path[name_end] != '/'; name_end++)
            ;
        printf("[sfs_open]ino: %d, str index: %d %d\n", ino, nameStart, name_end);
        bool ifok = 0;
        int entryNum = node->size / sizeof(struct sfs_entry); // entry的总数量
        // 直接索引
        for (int i = 0; i < node->blocks && i < SFS_NDIRECT; i++) {
            struct sfs_entry *entry = readBlock(node->direct[i]);
            // printf("node->direct[i]: %d\n", node->direct[i]);
            for (int j = 0; j < NUM_ENTRY && i * NUM_ENTRY + j < entryNum; j++) {
                // /   a  b c d e f g h   /
                //  index             next_slash
                if ((name_end - nameStart) != strlen(entry[j].filename))
                    continue;
                int k;
                for (k = 0; k < name_end - nameStart - 1; k++)
                    if (!entry[j].filename[k] || entry[j].filename[k] != path[nameStart + k])
                        break;
                if (entry[j].filename[k] == path[nameStart + k]) { // 找到了同名目录
                    nextino = entry[j].ino;
                    nextNode = (struct sfs_inode *)readBlock(entry[j].ino);
                    ifok = 1;
                    break;
                }
            }
            releaseBlock(node->direct[i]);
            if (ifok) // 如果已经找到就结束
                break;
        }
        printf("[sfs_open]%s\n", ifok == 0 ? "not find" : "find");
        // 找不到路径，尝试创建
        if (!ifok) {
            // 权限不够
            if (flags & SFS_FLAG_WRITE == 0)
                return -1;
            // 初始化新块
            nextino = NewBlock();
            nextNode = (struct sfs_inode *)readBlock(nextino);
            {
                // 目录要有两个entry，文件没有大小
                nextNode->size = path[name_end] ? sizeof(struct sfs_entry) << 1 : 0;
                // type需要看是不是路径的末尾
                nextNode->type = path[name_end] ? SFS_DIRECTORY : SFS_FILE;
                nextNode->links = 1;
                nextNode->blocks = 1;
                nextNode->direct[0] = NewBlock(); // 申请一个新的块
                nextNode->indirect = 0;
            }
            modifyBlock(nextino); // 设置成脏块
            printf("[sfs_open]nextino:%d, direct no:%d\n", nextino, nextNode->direct[0]);
            // 为文件夹添加两个默认相对路径
            if (nextNode->type == SFS_DIRECTORY) {
                struct sfs_entry *entry = (struct sfs_entry *)readBlock(nextNode->direct[0]);
                {
                    entry[0].ino = nextino;
                    strcpy(entry[0].filename, ".");
                    entry[1].ino = ino;
                    strcpy(entry[1].filename, "..");
                }
                modifyBlock(nextNode->direct[0]); // 设置成脏块
                releaseBlock(nextNode->direct[0]);
            }
            // 将这块给它接到父亲节点上
            struct sfs_entry newentry;
            for (int i = 0; i < name_end - nameStart; i++)
                newentry.filename[i] = path[nameStart + i];
            newentry.filename[name_end - nameStart] = 0;
            newentry.ino = nextino;
            if (node->size != node->blocks * sizeof(struct sfs_entry) * NUM_ENTRY) {     // 即最后一块不是满的
                struct sfs_entry *LastBlock = readBlock(node->direct[node->blocks - 1]); // 取出最后一块
                {
                    LastBlock[entryNum % NUM_ENTRY] = newentry;
                }
                modifyBlock(node->direct[node->blocks - 1]);
                releaseBlock(node->direct[node->blocks - 1]);
                // 不考虑间接索引
            } else { // 最后一块是满的，那就申请一个新块
                node->direct[node->blocks] = NewBlock();
                disk_write(node->direct[node->blocks], &newentry); // 写回新块，不用读到BufferPool里
                node->blocks++;
                // 不考虑间接索引
            }
            node->size += sizeof(struct sfs_entry); // 多了一个节点
            modifyBlock(ino);
        }

        // 释放掉老的PreNode
        if (preino != ino)
            releaseBlock(preino);
        preNode = node;
        preino = ino;
        node = nextNode;
        ino = nextino;
        nameStart = name_end + 1;
        printf("[sfs_open]ino: %d preino: %d\n", ino, preino);
    }
    writeBlock(preino);
    // 有同名目录
    if (node->type == SFS_DIRECTORY)
        return -1;
    // 创建文件，找一个空的指针填入
    for (int i = 0; i < 16; i++)
        if (current->fs.fds[i] == NULL) {
            current->fs.fds[i] = kmalloc(sizeof(struct file));
            current->fs.fds[i]->flags = flags;
            current->fs.fds[i]->inode = node;
            current->fs.fds[i]->off = 0;
            current->fs.fds[i]->path = preNode;
            current->fs.fds[i]->ino = ino;
            current->fs.fds[i]->path_ino = preino;
            printf("[sfs_open]success open a new file\n");
            return i;
        }
    return -1; // 打开了太多的文件
}

/**
 * 功能: 关闭一个文件，并将其修改过的内容写回磁盘
 * @fd  : 该进程打开的文件的 file descriptor (fd)
 * @ret : 正确关闭返回 0, 其他情况表示出错
 */
int sfs_close(int fd) {
    // 检查是否初始化
    if (!sfsInited && sfs_init())
        return -1;
    struct file *f = current->fs.fds[fd];
    // 检查文件是否打开
    if (!f) {
        printf("[sfs_close]file not opened\n");
        return -1;
    }
    // 将文件写回
    printf("[sfs_close]WriteBack file\n");
    writeInode(f->ino, SFS_FILE);

    // // 将路径上所有访问过的点及其数据块写回
    // printf("[sfs_close]WriteBack dirs\n");
    // uint32_t ino = f->path_ino;
    // while (ino > 1) {
    //     struct sfs_inode *node = (struct sfs_inode *)readBlock(ino);
    //     printf("[sfs_close]write ino %d\n", ino);
    //     writeInode(ino, SFS_DIRECTORY);
    //     struct sfs_entry *entrys = readBlock(node->direct[0]);
    //     uint32_t tmp = entrys[1].ino; // ..目录
    //     releaseBlock(node->direct[0]);
    //     releaseBlock(ino);
    //     ino = tmp;
    // }
    // printf("[sfs_close]write ino %d\n", ino);
    // writeInode(ino, SFS_DIRECTORY);

    // 写回bitmap和super块
    if (SFS.super_dirty) {
        disk_write(0, &SFS.super);
        disk_write(2, SFS.freemap->freemap);
        SFS.super_dirty = 0;
    }

    // 释放空间
    releaseBlock(f->ino);
    releaseBlock(f->path_ino);
    kfree(f);
    current->fs.fds[fd] = NULL;
    return 0;
}

/**
 * 功能  : 根据 fromwhere + off 偏移量来移动文件指针(可参考 C 语言的 fseek 函数功能)
 * @fd  : 该进程打开的文件的 file descriptor (fd)
 * @off : 偏移量
 * @fromwhere : SEEK_SET(文件头), SEEK_CUR(当前), SEEK_END(文件尾)
 * @ret : 表示错误码
 *        = 0 正确返回
 *        < 0 出错
 */
int sfs_seek(int fd, int32_t off, int fromwhere) {
    // 检查是否初始化
    if (!sfsInited && sfs_init())
        return -1;
    printf("[sfs_seek]fd = %d, off = %d, fromwhere = %d\n", fd, off, fromwhere);
    struct file *f = current->fs.fds[fd];
    // 检查文件是否打开
    if (!f) {
        printf("[sfs_seek]file not opened\n");
        return -1;
    }
    int32_t newOff;
    switch (fromwhere) {
    case SEEK_SET:
        newOff = off;
        break;
    case SEEK_END:
        newOff = f->inode->size - off;
        break;
    default:
        newOff = f->off + off;
        break;
    }
    // 越界
    if (newOff < 0 || newOff >= f->inode->size)
        return -1;
    f->off = newOff;
    return 0;
}

/**
 * 功能  : 从文件的文件指针开始读取 len 个字节到 buf 数组中 (结合 sfs_seek 函数使用)，并移动对应的文件指针
 * @fd  : 该进程打开的文件的 file descriptor (fd)
 * @buf : 读取内容的缓存区
 * @len : 要读取的字节的数量
 * @ret : 返回实际读取的字节的个数
 *        < 0 表示出错
 *        = 0 表示已经到了文件末尾，没有能读取的了
 *        > 0 表示实际读取的字节的个数，比如 len = 8，但是文件只剩 5 个字节的情况，就是返回 5
 */
int sfs_read(int fd, char *buf, uint32_t len) {
    // 检查是否初始化
    if (!sfsInited && sfs_init())
        return -1;
    struct file *f = current->fs.fds[fd];
    // 检查文件是否打开
    if (!f) {
        printf("[sfs_read]file not opened\n");
        return -1;
    }
    // printf("[sfs_read]remain: %d\n", f->inode->size - f->off);
    // 防止越界
    len = min(len, f->inode->size - f->off);
    if (len == 0)
        return 0;
    int blockindex = f->off / BLOCK_SIZE;
    int off = f->off % BLOCK_SIZE;
    uint32_t bufoff = 0;
    struct sfs_inode *node = f->inode;
    // 直接索引
    while (blockindex < SFS_NDIRECT && len) {
        uint32_t blocklen = min(len, BLOCK_SIZE - off);
        char *block = readBlock(node->direct[blockindex]);
        memcpy(buf + bufoff, block + off, blocklen);
        releaseBlock(node->direct[blockindex]);
        bufoff += blocklen;
        len -= blocklen;
        blockindex++;
        off = 0;
    }
    // 间接索引
    if (len) {
        blockindex -= SFS_NDIRECT;
        uint32_t *indirect = readBlock(node->indirect);
        while (len) {
            uint32_t blocklen = min(len, BLOCK_SIZE - off);
            char *block = readBlock(indirect[blockindex]);
            memcpy(buf + bufoff, block + off, blocklen);
            releaseBlock(indirect[blockindex]);
            bufoff += blocklen;
            len -= blocklen;
            blockindex++;
            off = 0;
        }
        releaseBlock(node->indirect);
    }
    f->off += bufoff;
    return bufoff;
}

/**
 * 功能  : 把 buf 数组的前 len 个字节写入到文件的文件指针位置(覆盖)(结合 sfs_seek 函数使用)，并移动对应的文件指针
 * @fd  : 该进程打开的文件的 file descriptor (fd)
 * @buf : 写入内容的缓存区
 * @len : 要写入的字节的数量
 * @ret : 返回实际的字节的个数
 *        < 0 表示出错
 *        >=0 表示实际写入的字节数量
 */
int sfs_write(int fd, char *buf, uint32_t len) {
    // 检查是否初始化
    if (!sfsInited && sfs_init())
        return -1;
    struct file *f = current->fs.fds[fd];
    // 检查文件是否打开
    if (!f) {
        printf("[sfs_write]file not opened\n");
        return -1;
    }
    if (f->flags & SFS_FLAG_WRITE == 0) {
        return -1; // 权限错误
    }
    int blockindex = f->off / BLOCK_SIZE;
    int off = f->off % BLOCK_SIZE;
    // printf("blockindex: %d off: %d\n", blockindex, off);
    uint32_t bufoff = 0;
    // 更新size
    if (len > f->inode->size - f->off) { // 超出了大小
        f->inode->size = f->off + len;   // 更新size
        modifyBlock(f->ino);
    }
    struct sfs_inode *node = f->inode;
    // 先搜索直接索引
    while (blockindex < SFS_NDIRECT && len) {
        if (blockindex >= node->blocks) { // 块超出范围
            node->direct[blockindex] = NewBlock();
            node->blocks++;
            modifyBlock(f->ino);
        }
        uint32_t blocklen = min(len, BLOCK_SIZE - off);
        char *block = readBlock(node->direct[blockindex]);
        memcpy(block + off, buf + bufoff, blocklen);
        modifyBlock(node->direct[blockindex]);
        // releaseBlock(node->direct[blockindex]);
        bufoff += blocklen;
        len -= blocklen;
        blockindex++;
        off = 0;
    }
    // 接下来间接索引,不考虑文件过大连间接索引都无法承载的情况
    if (len) {
        blockindex -= SFS_NDIRECT;
        if (node->indirect == 0) { // 如果没有indirect的节点还需要先申请
            node->indirect = NewBlock();
        }
        uint32_t *indirect = readBlock(node->indirect);
        while (len) {
            uint32_t blocklen = min(len, BLOCK_SIZE - off);
            if (blockindex + SFS_NDIRECT >= node->blocks) {
                indirect[blockindex] = NewBlock();
                node->blocks++;
                modifyBlock(node->indirect);
            }
            char *block = readBlock(indirect[blockindex]);
            memcpy(block + off, buf + bufoff, blocklen);
            modifyBlock(indirect[blockindex]);
            // releaseBlock(indirect[blockindex]);
            bufoff += blocklen;
            len -= blocklen;
            blockindex++;
            off = 0;
        }
        // releaseBlock(node->indirect);
    }
    f->off += bufoff;
    return bufoff;
}

/**
 * 功能    : 获取 path 下的所有文件名，并存储在 files 数组中
 * @path  : 文件夹路径 (绝对路径)
 * @files : 保存该文件夹下所有的文件名
 * @ret   : > 0 表示该文件夹下有多少文件
 *          = 0 表示该 path 是一个文件
 *          < 0 表示出错
 */
int sfs_get_files(const char *path, char *files[]) {
    // 检查是否初始化
    if (!sfsInited && sfs_init())
        return -1;
    printf("sfs_get_files path: %s\n", path);
    // 路径必须从根目录开始
    if (path[0] != '/')
        return -1;

    struct sfs_inode *node = readBlock(1), *preNode = NULL,
                     *nextNode = NULL; // PreNode存储上一个节点
    int nameStart = 1, name_end = 1;
    uint32_t ino = 1, preino = 1, nextino = 1;
    while (path[name_end]) {
        if (path[nameStart] == '/')
            return -1; // /连用，无法识别
        else if (path[nameStart] == 0)
            break; // 合法情况，如/home/
        // 如果上层目录是文件
        if (node->type == SFS_FILE)
            return -1;
        // 找到下一个'/'的位置
        for (name_end += 1; path[name_end] && path[name_end] != '/'; name_end++)
            ;
        printf("[sfs_open]ino: %d, str index: %d %d\n", ino, name_end, nameStart);
        bool ifok = 0;
        int entryNum = node->size / sizeof(struct sfs_entry); // entry的总数量
        // 直接索引
        for (int i = 0; i < node->blocks && i < SFS_NDIRECT; i++) {
            struct sfs_entry *entry = readBlock(node->direct[i]);
            // printf("node->direct[i]: %d\n", node->direct[i]);
            for (int j = 0; j < NUM_ENTRY && i * NUM_ENTRY + j < entryNum; j++) {
                // /   a  b c d e f g h   /
                //  index             next_slash
                if ((name_end - nameStart) != strlen(entry[j].filename))
                    continue;
                int k;
                for (k = 0; k < name_end - nameStart - 1; k++)
                    if (!entry[j].filename[k] || entry[j].filename[k] != path[nameStart + k])
                        break;
                if (entry[j].filename[k] == path[nameStart + k]) { // 找到了同名目录
                    nextino = entry[j].ino;
                    nextNode = (struct sfs_inode *)readBlock(entry[j].ino);
                    ifok = 1;
                    break;
                }
            }
            releaseBlock(node->direct[i]);
            if (ifok) // 如果已经找到就结束
                break;
        }
        printf("[sfs_open]%s\n", ifok == 0 ? "not find" : "find");
        // 找不到路径
        if (!ifok) {
            return -1;
        }

        // 释放掉老的PreNode
        if (preino != ino)
            releaseBlock(preino);
        preNode = node;
        preino = ino;
        node = nextNode;
        ino = nextino;
        nameStart = name_end + 1;
        printf("[sfs_open]ino: %d preino: %d\n\n\n", ino, preino);
    }

    if (node->type == SFS_FILE)
        return 0; // 当前目录是一个文件

    uint32_t tot;
    int numBlock = (node->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    // 直接索引
    for (int i = 0; i < SFS_NDIRECT && i < numBlock; i++) {
        struct sfs_entry *entrys = readBlock(node->direct[i]);
        for (int j = 0; j < NUM_ENTRY && i * BLOCK_SIZE + j * sizeof(struct sfs_entry) < node->size; j++)
            strcpy(files[tot++], entrys[j].filename);
        releaseBlock(node->direct[i]);
    }
    // 间接索引
    if (numBlock > SFS_NDIRECT) {
        uint32_t *indirect = readBlock(node->indirect);
        for (int i = SFS_NDIRECT; i < numBlock; i++) {
            struct sfs_entry *entrys = readBlock(indirect[i - SFS_NDIRECT]);
            for (int j = 0; j < NUM_ENTRY && i * BLOCK_SIZE + j * sizeof(struct sfs_entry) < node->size; j++)
                strcpy(files[tot++], entrys[j].filename);
            releaseBlock(indirect[i - SFS_NDIRECT]);
        }
        releaseBlock(node->indirect);
    }
    return tot;
}