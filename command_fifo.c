#include "command_fifo.h"
#include <string.h>

/**
 * @brief 初始化命令 FIFO
 *
 * 将读写指针和计数器清零，准备存储命令。
 *
 * @param fifo 指向 FIFO 对象
 */
void command_fifo_init(command_fifo_t *fifo) 
{
    fifo->head = 0;
    fifo->tail = 0;
    fifo->count = 0;
}

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
bool command_fifo_push(command_fifo_t *fifo, const char *cmd, uint16_t len) 
{
    if (command_fifo_is_full(fifo) || len > CMD_MAX_LENGTH) 
    {
        return false;
    }
    memcpy(fifo->buffer[fifo->tail], cmd, len);
    fifo->length[fifo->tail] = len;
    fifo->tail = (fifo->tail + 1) % CMD_FIFO_SIZE;
    fifo->count++;
    return true;
}

/**
 * @brief 从 FIFO 中弹出一条命令
 *
 * @param fifo 指向 FIFO 对象
 * @param buf  用于存放命令字符串的缓冲区
 * @param len  返回命令长度
 * @return true 表示成功弹出，false 表示 FIFO 为空
 */
bool command_fifo_pop(command_fifo_t *fifo, char *buf, uint16_t *len) 
{
    if (command_fifo_is_empty(fifo)) 
    {
        return false;
    }
    *len = fifo->length[fifo->head];
    memcpy(buf, fifo->buffer[fifo->head], *len);
    fifo->head = (fifo->head + 1) % CMD_FIFO_SIZE;
    fifo->count--;
    return true;
}

/**
 * @brief 判断 FIFO 是否为空
 *
 * @param fifo 指向 FIFO 对象
 * @return true 表示为空，false 表示非空
 */
bool command_fifo_is_empty(command_fifo_t *fifo) 
{
    return fifo->count == 0;
}

/**
 * @brief 判断 FIFO 是否已满
 *
 * @param fifo 指向 FIFO 对象
 * @return true 表示已满，false 表示未满
 */
bool command_fifo_is_full(command_fifo_t *fifo) 
{
    return fifo->count >= CMD_FIFO_SIZE;
}
