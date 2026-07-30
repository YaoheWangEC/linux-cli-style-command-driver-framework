#include "command_io.h"

#include "at32f421_wk_config.h"
#include "at32f421_gpio.h"
#include "at32f421_usart.h"
#include "wk_dma.h"

#include <string.h>

#define DMA_RX_CHANNEL DMA1_CHANNEL5
#define DMA_TX_CHANNEL DMA1_CHANNEL4
#define IO_USART USART2

uint8_t rx_data_buf[COMMAND_IO_RX_BUF_SIZE]; // 接收数据缓冲区
volatile uint8_t rx_data_len; // 接收数据缓冲区的内容长度
uint8_t rx_dma_buf[COMMAND_IO_RX_BUF_SIZE]; // DMA RX 缓冲区

uint8_t tx_data_buf[COMMAND_IO_TX_BUF_SIZE]; // 发送数据缓冲区
volatile uint8_t tx_data_len; // 发送数据缓冲区的内容长度
uint8_t tx_dma_buf[COMMAND_IO_TX_BUF_SIZE]; // DMA TX 缓冲区
volatile bool tx_cplt;

static command_fifo_t cmd_fifo; // 命令 FIFO

/**
  * @brief  校验是否为合法的命令字符
  * @param  ch 待校验字符
  * @retval true  合法字符 (0x20~0x7E 或 \\r \\n)
  * @retval false 非法字符
  */
static bool is_valid_char(char ch)
{
    if (ch == '\r' || ch == '\n') return true; // 提前处理换行符
    if (ch < 0x20 || ch >= 0x7f) return false; // 不可显示的字符
    return true;
}

/**
  * @brief  设置 RS485 为发送模式 (DE 拉高)
  */
static void command_io_set_tx_mode(void)
{
    gpio_bits_set(RS485_DE_GPIO_PORT, RS485_DE_PIN);
}

/**
  * @brief  设置 RS485 为接收模式 (DE 拉低)
  */
static void command_io_set_rx_mode(void)
{
    gpio_bits_reset(RS485_DE_GPIO_PORT, RS485_DE_PIN);
}

/**
  * @brief  初始化命令 IO 模块
  *
  * 清零缓冲区与状态, 初始化 RS485 为接收模式,
  * 配置 DMA RX 通道 (normal 模式) 并使能 IDLE 中断。
  */
void command_io_init(void)
{
    rx_data_len = 0;
    tx_data_len = 0;
    tx_cplt = true;

    command_io_set_rx_mode();

    command_fifo_init(&cmd_fifo);

    usart_dma_receiver_enable(IO_USART, TRUE); 
    usart_dma_transmitter_enable(IO_USART, TRUE);

    dma_channel_enable(DMA_RX_CHANNEL, FALSE); // 禁用 USART_RX 的 DMA 接收
    usart_flag_clear(IO_USART, USART_IDLEF_FLAG); // 强制清除 IDLE 标志
    
    // 清空残留的接收数据
    while(usart_flag_get(IO_USART, USART_RDBF_FLAG) != RESET)
    {
        volatile uint32_t dummy = IO_USART->dt;
        (void)dummy; // 读出丢弃
    }

    wk_dma_channel_config(DMA_RX_CHANNEL, 
        (uint32_t)&IO_USART->dt, 
        (uint32_t)rx_dma_buf, 
        sizeof(rx_dma_buf)); // 配置DMA接收通道

    dma_channel_enable(DMA_RX_CHANNEL, TRUE); // 打开 USART_RX 的 DMA 接收
    usart_interrupt_enable(IO_USART, USART_IDLE_INT, TRUE); // 使能 UART 空闲中断
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
  * @retval false 发送已完成, 可启动下一次发送
  */
bool command_io_is_tx_busy(void)
{
    return !tx_cplt;
}

/**
  * @brief  启动发送 (非阻塞)
  * @retval true  发送已启动
  * @retval false 发送缓冲区为空, 或上一次发送尚未完成
  *
  * 将 tx_data_buf 拷贝到 DMA 缓冲区, 切换 TX 模式,
  * 配置并启动 DMA TX 通道, 使能 DMA 传输完成中断。
  * 发送完成后 ISR 自动拉低 DE 并置 tx_cplt = true。
  */
bool command_io_transmit_start(void)
{
    // 发送缓冲区空或正在发送则禁止发送新数据
    if ((tx_data_len == 0) || (tx_cplt == false))
    {
        return false;
    }

    // 加载缓冲区
    memcpy(tx_dma_buf, tx_data_buf, tx_data_len);
    tx_cplt = false;
    uint32_t tx_len = tx_data_len;
    tx_data_len = 0;
    command_io_set_tx_mode();

    // 配置 DMA 发送
    dma_channel_enable(DMA_TX_CHANNEL, FALSE); // 先关闭 DMA 通道，避免残留状态
    wk_dma_channel_config(DMA_TX_CHANNEL, 
        (uint32_t)&IO_USART->dt, 
        (uint32_t)tx_dma_buf, 
        (uint16_t)tx_len);
    dma_channel_enable(DMA_TX_CHANNEL, TRUE);

    // 打开 DMA_TX_CHANNEL 的完成中断
    dma_interrupt_enable(DMA_TX_CHANNEL, DMA_FDT_INT, TRUE);

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
  * 由 USART 的 IDLE 中断和 RX DMA 满中断调用。
  * 将 DMA 硬件缓冲区数据拷入 rx_data_buf,
  * 检测 \\r\\n 命令结束符后压入命令 FIFO, 最后重启 DMA RX。
  */
void command_io_rx_handler(void)
{
    uint32_t rcv_len = sizeof(rx_dma_buf) - dma_data_number_get(DMA_RX_CHANNEL);
    dma_channel_enable(DMA_RX_CHANNEL, FALSE); // 先读状态再关DMA防止关闭后归位

    // 若可全部填入缓冲区则压入 rx_data_buf
    if ((rcv_len + rx_data_len) <= COMMAND_IO_RX_BUF_SIZE)
    {
        for (uint8_t i = 0; i < rcv_len; i++)
        {
            uint8_t ch = rx_dma_buf[i];
    
            // 丢弃不可打印字符 (除 \r \n)
            if (!is_valid_char((char)ch))
                continue;
    
            rx_data_buf[rx_data_len] = ch;
            rx_data_len++;
        }
    }
    else
    {
        // 溢出,丢弃全部内容
        rx_data_len = 0;
    }

    // 检查是否存在 \r\n 结束符
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

    // 重置 DMA 计数器
    dma_data_number_set(DMA_RX_CHANNEL, sizeof(rx_dma_buf));
    dma_channel_enable(DMA_RX_CHANNEL, TRUE);
}

/**
  * @brief  数据发送中断处理
  *
  * 由 USART TX DMA 中断调用。
  * 等待 USART 移位寄存器全部发送完毕 (TDC 标志),
  * 回到接收模式, 置 tx_cplt = true。
  */
void command_io_tx_handler(void)
{
    // DMA发送完毕，等待数据全部清空
    while (usart_flag_get(IO_USART, USART_TDC_FLAG) == RESET);

    printf("tx finish\r\n");

    // 切换接收模式
    command_io_set_rx_mode();
    tx_cplt = true;
}
