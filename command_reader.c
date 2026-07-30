#include "command_reader.h"
#include "command_fifo.h"
#include "global_vars.h"
#include "usb_core.h"
#include "cdc_class.h"
#include <string.h>
#include <stdio.h>

#define RX_BUFFER_LENGTH 128

static command_fifo_t cmd_fifo;

static char rx_buffer[RX_BUFFER_LENGTH];
static uint16_t rx_index = 0;


static bool is_valid_char(char ch)
{
    if (ch == '\r' || ch == '\n') return true; // 提前处理换行符
    if (ch < 0x20 || ch >= 0x7f) return false; // 不可显示的字符
    return true;
}

/**
 * @brief 初始化命令读取模块
 *
 * 初始化内部命令 FIFO，用于存储接收到的完整命令字符串。
 * 在系统启动时调用一次。
 */
void command_reader_init(void) 
{
    command_fifo_init(&cmd_fifo);
}

/**
 * @brief 命令读取任务
 *
 * 从 USB CDC 接口读取数据，逐字节检查并拼接到接收缓冲区。
 * 当检测到以 "\r\n" 结尾的完整命令时，将其压入内部 FIFO。
 * 超长命令或非法字符会被丢弃。
 */
void command_reader_task(void)
{
    uint8_t buf[64];
    uint32_t len = usb_vcp_get_rxdata(&g_vars.usb_fs_handle->dev, buf);

    if (len > 0)
    {
        for (uint32_t i = 0; i < len; i++)
        {
            uint8_t ch = buf[i];

            // 检查是否溢出
            if (rx_index >= RX_BUFFER_LENGTH - 1)
            {
                // 超长命令，直接丢弃
                printf("Command too long, discarded\n");
                rx_index = 0;
                continue;
            }

            // 必须为可合法字符
            if (is_valid_char(ch))
            {
                rx_buffer[rx_index] = ch;
                rx_index++;
            }

            // 检测到 \r\n 结束符
            if (rx_index >= 2 && rx_buffer[rx_index - 1] == '\n' && rx_buffer[rx_index - 2] == '\r')
            {
                uint16_t cmd_len = rx_index - 2; // 去掉 \r\n
                rx_buffer[cmd_len] = '\0';       // 补上 \0
                cmd_len += 1;

                // 丢弃超长命令
                if (cmd_len > CMD_MAX_LENGTH)
                {
                    printf("Command too long, discarded\n");
                    continue;
                }

                if (!command_fifo_push(&cmd_fifo, rx_buffer, cmd_len))
                {
                    printf("FIFO full, command dropped\n");
                }
                else
                {
                    // printf("Command received, length=%u\n", cmd_len);
                }

                // 重置索引，准备下一条命令
                rx_index = 0;
            }
        }
    }
}

/**
 * @brief 从命令 FIFO 中取出一条命令
 *
 * @param buf  用于存放命令字符串的缓冲区
 * @param len  返回命令长度
 * @return true 表示成功取出命令，false 表示 FIFO 为空
 *
 * 上层模块调用此函数获取待解析的命令。
 */
bool command_reader_pop(char *buf, uint16_t *len) 
{
    return command_fifo_pop(&cmd_fifo, buf, len);
}
