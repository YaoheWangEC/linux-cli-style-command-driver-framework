#ifndef COMMAND_READER_H
#define COMMAND_READER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 初始化命令读取模块
 *
 * 初始化内部命令 FIFO，用于存储接收到的完整命令字符串。
 * 在系统启动时调用一次。
 */
void command_reader_init(void);

/**
 * @brief 命令读取任务
 *
 * 从 USB CDC 接口读取数据，逐字节检查并拼接到接收缓冲区。
 * 当检测到以 "\r\n" 结尾的完整命令时，将其压入内部 FIFO。
 * 超长命令或非法字符会被丢弃。
 */
void command_reader_task(void);

/**
 * @brief 从命令 FIFO 中取出一条命令
 *
 * @param buf  用于存放命令字符串的缓冲区
 * @param len  返回命令长度
 * @return true 表示成功取出命令，false 表示 FIFO 为空
 *
 * 上层模块调用此函数获取待解析的命令。
 */
bool command_reader_pop(char *buf, uint16_t *len);

#endif
