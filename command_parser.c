#include "command_parser.h"
#include "command_io.h"
#include "command_table.h"
#include "command_handler.h"
#include <string.h>
#include <stdio.h>

/**
  * @brief  通过通信接口发送响应字符串 (支持超长分帧)
  * @param  msg 要发送的字符串 (以 \0 结尾)
  *
  * 将响应按 COMMAND_IO_TX_BUF_SIZE 切分为多帧依次发送,
  * 每帧等待前一帧完成后再启动下一帧, 全部帧发送完毕后返回。
  */
static void command_send_response(const char *msg)
{
    uint32_t total_len = strlen(msg);
    uint32_t sent = 0;

    while (sent < total_len)
    {
        while (command_io_is_tx_busy());

        uint32_t chunk = total_len - sent;
        if (chunk > COMMAND_IO_TX_BUF_SIZE)
            chunk = COMMAND_IO_TX_BUF_SIZE;

        command_io_put_buf(msg + sent, chunk);
        command_io_transmit_start();
        sent += chunk;
    }

    while (command_io_is_tx_busy());
}

/**
 * @brief 初始化命令解析器
 *
 * 注册所有支持的命令。
 */
void command_parser_init(void)
{
    command_table_init();
    register_command("lscmd", lscmd_handler);
    register_command("echo", echo_handler);
}

/**
 * @brief 命令解析任务
 *
 * 从命令 FIFO 中取出一条命令，调用命令表执行，并将结果通过通信接口返回。
 */
void command_parser_task(void)
{
    char buf[128];
    uint16_t len;

    if (command_io_pop(buf, &len))
    {
        char *output = NULL;
        execute_command(buf, &output);
        command_send_response(output);
    }
}


