#include "command_io.h"

#include "cdc_class.h"
#include "user.h"

#include <string.h>


static uint8_t  rx_data_buf[COMMAND_IO_RX_BUF_SIZE];   // 接收拼包缓冲区
static volatile uint8_t rx_data_len;                   // 接收缓冲区有效长度
static uint8_t  tx_data_buf[COMMAND_IO_TX_BUF_SIZE];   // 发送队列缓冲区
static volatile uint8_t tx_data_len;                   // 发送队列有效长度

static command_fifo_t cmd_fifo;                        // 命令 FIFO

/**
  * @brief 获取 usbd_core_type 句柄
  */
static void *usb_cdc_get_dev(void)
{
    otg_core_type *core = usb_get_core_fs1();
    return &core->dev;
}

/**
  * @brief  校验是否为合法的命令字符
  * @param  ch 待校验字符
  * @retval true  合法字符 (0x20~0x7E 或 \r \n)
  * @retval false 非法字符
  */
static bool is_valid_char(char ch)
{
    if (ch == '\r' || ch == '\n') return true;
    if (ch < 0x20 || ch >= 0x7f) return false;
    return true;
}

/**
  * @brief  初始化命令 IO 模块 
  *
  * 清零缓冲区与状态, 初始化命令 FIFO。
  * 注意: 本模块通过轮询 usb_vcp_get_rxdata() 获取数据,
  *       而非中断驱动。调用方需在主循环中周期性调用
  *       command_io_rx_handler()。
  */
void command_io_init(void)
{
    rx_data_len = 0;
    tx_data_len = 0;

    command_fifo_init(&cmd_fifo);
}

/**
  * @brief  从接收缓冲区取出一个字节
  * @param  c 指向存放读取结果的指针
  * @retval true  成功读取
  * @retval false 接收缓冲区为空
  */
bool command_io_get_char(uint8_t *c)
{
    if (rx_data_len == 0)
        return false;
    *c = rx_data_buf[0];
    rx_data_len--;
    if (rx_data_len > 0)
        memmove(rx_data_buf, rx_data_buf + 1, rx_data_len);
    return true;
}

/**
  * @brief  向发送缓冲区压入一个字节
  * @param  c 待发送的字节
  * @retval true  压入成功
  * @retval false 发送缓冲区已满
  * @note   仅缓冲数据, 需调用 command_io_transmit_start() 启动发送。
  */
bool command_io_put_char(uint8_t c)
{
    if (tx_data_len >= COMMAND_IO_TX_BUF_SIZE)
        return false;
    tx_data_buf[tx_data_len] = c;
    tx_data_len++;
    return true;
}

/**
  * @brief  向发送缓冲区压入一个字符串
  * @param  str 待发送的字符串 (以 \\0 结尾)
  * @retval true  压入成功
  * @retval false 发送缓冲区剩余空间不足, 整体丢弃
  * @note   原子操作: 要么整体压入, 要么整体丢弃, 不会部分写入。
  */
bool command_io_put_string(const char *str)
{
    size_t len = strlen(str);
    if (tx_data_len + len > COMMAND_IO_TX_BUF_SIZE)
        return false;
    memcpy(tx_data_buf + tx_data_len, str, len);
    tx_data_len += len;
    return true;
}

/**
  * @brief  向发送缓冲区压入指定长度的数据
  * @param  buf 待发送数据指针
  * @param  len 数据长度 (字节)
  * @retval true  压入成功
  * @retval false 发送缓冲区剩余空间不足, 整体丢弃
  * @note   与 command_io_put_string 不同, 本函数不依赖 \0 结尾,
  *         适合超长响应分帧发送场景。原子操作: 要么整体压入, 要么整体丢弃。
  */
bool command_io_put_buf(const char *buf, uint32_t len)
{
    if (tx_data_len + len > COMMAND_IO_TX_BUF_SIZE)
        return false;
    memcpy(tx_data_buf + tx_data_len, buf, len);
    tx_data_len += len;
    return true;
}

/**
  * @brief  查询发送是否空闲
  * @retval true  发送正在进行中
  * @retval false 发送已完成
  *
  * USB CDC 模式下 transmit_start 内部阻塞等待,
  * 返回后数据已发出, 故本函数始终返回 false。
  */
bool command_io_is_tx_busy(void)
{
    return false;
}

/**
  * @brief  启动发送 (非阻塞)
  * @retval true  发送已启动
  * @retval false 发送缓冲区为空, 或上一次发送尚未完成
  *
  * 将 tx_data_buf 通过 usb_vcp_send_data() 发出。
  * 内部轮询等待 USB CDC IN 端点就绪, 正常 USB FS BULK
  * 传输在 1~2 帧内完成 (< 2 ms), 超时仅出现在主机未轮询
  * 或总线挂起等异常场景。
  */
bool command_io_transmit_start(void)
{
    if (tx_data_len == 0)
        return false;

    void *usb_dev = usb_cdc_get_dev();

    /*
     * 轮询等待 USB CDC IN 端点可用。
     * 正常 USB FS BULK 传输在 1~2 帧内完成 (< 2 ms),
     * 超时仅出现在主机未轮询或总线挂起等异常场景。
     */
    int32_t timeout = 10000000;
    while (usb_vcp_send_data(usb_dev, tx_data_buf, tx_data_len) != SUCCESS)
    {
        if (--timeout == 0)
            return false;
    }

    tx_data_len = 0;
    return true;
}

/**
  * @brief  清空收发缓冲区 (丢弃所有未处理数据)
  */
void command_io_flush(void)
{
    tx_data_len = 0;
    rx_data_len = 0;
}

/**
  * @brief  从命令 FIFO 中取出一条完整命令
  * @param  buf 用于存放命令字符串的缓冲区
  * @param  len 返回命令长度 (含 \\0 结束符)
  * @retval true  成功取出
  * @retval false FIFO 为空
  */
bool command_io_pop(char *buf, uint16_t *len)
{
    return command_fifo_pop(&cmd_fifo, buf, len);
}

/**
  * @brief  数据接收中断处理
  *
  * 轮询 usb_vcp_get_rxdata() 获取 USB 主机下发的数据,
  * 过滤非法字符后拼入 rx_data_buf,
  * 检测到 \r\n 命令结束符后压入命令 FIFO。
  *
  * 调用频率: 建议在主循环中不低于 100 Hz。
  */
void command_io_rx_handler(void)
{
    uint8_t usb_pkt[USBD_CDC_OUT_MAXPACKET_SIZE];
    void   *usb_dev = usb_cdc_get_dev();

    uint16_t rcv_len = usb_vcp_get_rxdata(usb_dev, usb_pkt);
    if (rcv_len == 0)
        return;

    // 容量检查后逐字节拷入
    if ((rcv_len + rx_data_len) <= COMMAND_IO_RX_BUF_SIZE)
    {
        for (uint16_t i = 0; i < rcv_len; i++)
        {
            uint8_t ch = usb_pkt[i];

            // 丢弃不可打印字符 (除 \r \n)
            if (!is_valid_char((char)ch))
                continue;

            rx_data_buf[rx_data_len] = ch;
            rx_data_len++;
        }
    }
    else
    {
        // 溢出, 丢弃全部内容
        rx_data_len = 0;
    }

    // 检测 \r\n 命令结束符
    if (rx_data_len >= 2
        && rx_data_buf[rx_data_len - 2] == '\r'
        && rx_data_buf[rx_data_len - 1] == '\n')
    {
        uint16_t cmd_len = rx_data_len - 2;   // 去掉 \r\n
        rx_data_buf[cmd_len] = '\0';          // 补上字符串结束符
        cmd_len += 1;                         // 包含 \0 的长度

        if (cmd_len <= CMD_MAX_LENGTH)
        {
            printf("cmd length = %d\r\n", cmd_len);
            command_fifo_push(&cmd_fifo, (char *)rx_data_buf, cmd_len);
        }
        rx_data_len = 0;
    }
}

/**
  * @brief  数据发送中断处理
  *
  * USB CDC 的发送完成由 USB 协议栈的 class_in_handler 中断
  * 内部处理 (置 g_tx_completed), 本模块无需额外操作。
  * 保留此函数仅为兼容 command_io.h 接口。
  */
void command_io_tx_handler(void)
{
    // USB CDC: TX 完成由 USB 栈 ISR 内部处理, 此处无操作
}
