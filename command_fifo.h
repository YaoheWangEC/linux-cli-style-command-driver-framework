#ifndef COMMAND_FIFO_H
#define COMMAND_FIFO_H

#include <stdint.h>
#include <stdbool.h>

#define CMD_FIFO_SIZE 4       // FIFO 容量
#define CMD_MAX_LENGTH 64     // 单条命令最大长度

typedef struct 
{
    char buffer[CMD_FIFO_SIZE][CMD_MAX_LENGTH]; // 命令存储区
    uint16_t length[CMD_FIFO_SIZE];             // 每条命令长度
    uint8_t head;                               // 读指针
    uint8_t tail;                               // 写指针
    uint8_t count;                              // 当前命令数量
} command_fifo_t;

/**
 * @brief 初始化命令 FIFO
 *
 * 将读写指针和计数器清零，准备存储命令。
 *
 * @param fifo 指向 FIFO 对象
 */
void command_fifo_init(command_fifo_t *fifo);

/**
 * @brief 将命令压入 FIFO
 *
 * @param fifo 指向 FIFO 对象
 * @param cmd  命令字符串
 * @param len  命令长度
 * @return true 表示成功压入，false 表示 FIFO 满或命令超长
 *
 * 注意：命令长度必须小于 CMD_MAX_LENGTH。
 */
bool command_fifo_push(command_fifo_t *fifo, const char *cmd, uint16_t len);

/**
 * @brief 从 FIFO 中弹出一条命令
 *
 * @param fifo 指向 FIFO 对象
 * @param buf  用于存放命令字符串的缓冲区
 * @param len  返回命令长度
 * @return true 表示成功弹出，false 表示 FIFO 为空
 */
bool command_fifo_pop(command_fifo_t *fifo, char *buf, uint16_t *len);

/**
 * @brief 判断 FIFO 是否为空
 *
 * @param fifo 指向 FIFO 对象
 * @return true 表示为空，false 表示非空
 */
bool command_fifo_is_empty(command_fifo_t *fifo);

/**
 * @brief 判断 FIFO 是否已满
 *
 * @param fifo 指向 FIFO 对象
 * @return true 表示已满，false 表示未满
 */
bool command_fifo_is_full(command_fifo_t *fifo);

#endif // COMMAND_FIFO_H
