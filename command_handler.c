#include "command_handler.h"
#include "command_table.h"
#include "bsp.h"
#include "global_vars.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static char resp[1024];

#define M_PI 3.14159265f

char* lscmd_handler(int argc, char **argv)
{
    // 帮助信息
    if (argc == 2 && 
       (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        sprintf(resp,
            "Usage: lscmd [options]\r\n"
            "Options:\r\n"
            "  -h, --help    Show this help message\r\n"
            "\r\n"
            "List all supported commands.\r\n");
        return resp;
    }
    
    if (argc == 1)
    {
        // 默认行为：列出所有命令
        int count = command_table_count();
        int offset = snprintf(resp, sizeof(resp), "Supported commands:\r\n");
        for (int i = 0; i < count && offset < sizeof(resp); i++)
        {
            const char* name = command_table_get_name(i);
            if (name)
            {
                offset += snprintf(resp + offset, sizeof(resp) - offset, "  %s\r\n", name);
            }
        }
        return resp;
    }

    // 无效参数
    sprintf(resp, "Invalid option. Try 'lscmd -h'\r\n");
    return resp;
}

char* echo_handler(int argc, char **argv)
{
    // 帮助信息
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        snprintf(resp, sizeof(resp),
                 "Usage: echo [text]\r\n"
                 "Options:\r\n"
                 "  -h, --help    Show this help message\r\n"
                 "\r\n"
                 "Print the given text to output.\r\n");
        return resp;
    }

    // 默认行为：拼接参数并返回
    if (argc > 1)
    {
        resp[0] = '\0';
        for (int i = 1; i < argc; i++)
        {
            strncat(resp, argv[i], sizeof(resp) - strlen(resp) - 1);
            if (i < argc - 1)
                strncat(resp, " ", sizeof(resp) - strlen(resp) - 1);
        }
        strncat(resp, "\r\n", sizeof(resp) - strlen(resp) - 1);
        return resp;
    }

    snprintf(resp, sizeof(resp), "\r\n");
    return resp;
}

char* adc_handler(int argc, char **argv)
{
    // 帮助信息
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        snprintf(resp, sizeof(resp),
            "Usage: adc <subcommand> [args]\r\n"
            "Subcommands:\r\n"
            "  start              Start ADC capture\r\n"
            "  stop               Stop ADC capture\r\n"
            "  rate [48000|24000|16000|8000|1000]\r\n"
            "                     Set or get sample rate (Hz)\r\n"
            "  channel [left|right|both]\r\n"
            "                     Set or get capture channel\r\n"
            "  info               Show ADC configuration and status\r\n"
            "  -h, --help         Show this help message\r\n");
        return resp;
    }

    // adc (无参默认)
    if (argc == 1)
    {
        snprintf(resp, sizeof(resp),
            "ADC: missing subcommand. Try 'adc -h'\r\n");
        return resp;
    }

    // adc start
    if (strcmp(argv[1], "start") == 0)
    {
        if (g_vars.adc_running)
        {
            snprintf(resp, sizeof(resp),
                "ADC: already running, rate=%lu Hz\r\n", g_vars.adc_samplerate);
            return resp;
        }
        if (g_vars.adc_start_countdown_enable)
        {
            snprintf(resp, sizeof(resp),
                "ADC: start already pending, %ld ms remaining\r\n",
                (int32_t)(g_vars.adc_start_countdown_tick - g_vars.tick));
            return resp;
        }
        g_vars.adc_stream_dest = ADC_STREAM_USB;
        g_vars.adc_start_countdown_tick = g_vars.tick + 1000;
        g_vars.adc_start_countdown_enable = true;

        snprintf(resp, sizeof(resp),
            "ADC start pending, will start in 1000 ms, rate=%lu Hz\r\n",
            g_vars.adc_samplerate);
        return resp;
    }

    // adc stop
    if (strcmp(argv[1], "stop") == 0)
    {
        // 取消待处理的延迟启动
        if (g_vars.adc_start_countdown_enable)
        {
            g_vars.adc_start_countdown_enable = false;
            g_vars.adc_start_countdown_tick = 0;
            g_vars.adc_stream_dest = ADC_STREAM_NONE;
            snprintf(resp, sizeof(resp), "ADC start cancelled\r\n");
            return resp;
        }
        if (!g_vars.adc_running)
        {
            snprintf(resp, sizeof(resp), "ADC: already stopped\r\n");
            return resp;
        }
        g_vars.adc_stream_dest = ADC_STREAM_NONE;
        bsp_adc_stop();
        snprintf(resp, sizeof(resp), "ADC stopped\r\n");
        return resp;
    }

    // adc rate
    if (strcmp(argv[1], "rate") == 0)
    {
        if (argc == 3)
        {
            uint32_t rate = (uint32_t)atoi(argv[2]);
            if (bsp_adc_set_samplerate(rate))
            {
                snprintf(resp, sizeof(resp),
                    "ADC rate set to %lu Hz\r\n", g_vars.adc_samplerate);
            }
            else
            {
                snprintf(resp, sizeof(resp),
                    "ADC rate: invalid value %s. Supported: 48000, 24000, 16000, 8000, 1000\r\n",
                    argv[2]);
            }
        }
        else
        {
            snprintf(resp, sizeof(resp),
                "ADC rate: %lu Hz\r\n", g_vars.adc_samplerate);
        }
        return resp;
    }

    // adc channel
    if (strcmp(argv[1], "channel") == 0)
    {
        if (g_vars.adc_running)
        {
            snprintf(resp, sizeof(resp),
                "ADC channel: cannot change while running. Stop ADC first.\r\n");
            return resp;
        }

        if (argc == 3)
        {
            bool left, right;
            if (strcmp(argv[2], "left") == 0)
            {
                left = true; right = false;
            }
            else if (strcmp(argv[2], "right") == 0)
            {
                left = false; right = true;
            }
            else if (strcmp(argv[2], "both") == 0)
            {
                left = true; right = true;
            }
            else
            {
                snprintf(resp, sizeof(resp),
                    "ADC channel: invalid value '%s'. Supported: left, right, both\r\n",
                    argv[2]);
                return resp;
            }

            if (bsp_adc_set_channel(left, right))
            {
                snprintf(resp, sizeof(resp),
                    "ADC channel set to %s\r\n",
                    (left && right) ? "L+R" : (left ? "L" : "R"));
            }
            else
            {
                snprintf(resp, sizeof(resp),
                    "ADC channel: failed to set\r\n");
            }
        }
        else
        {
            snprintf(resp, sizeof(resp),
                "ADC channel: %s\r\n",
                (g_vars.adc_left_channel && g_vars.adc_right_channel) ? "L+R" :
                (g_vars.adc_left_channel ? "L" : "R"));
        }
        return resp;
    }

    // adc info
    if (strcmp(argv[1], "info") == 0)
    {
        uint32_t in  = g_vars.adc_buffer_input_index;
        uint32_t out = g_vars.adc_buffer_output_index;
        uint32_t used;
        if (in >= out)
            used = in - out;
        else
            used = ADC_BUFFER_LENGTH_HALFWORD - out + in;

        uint32_t pct = (used * 100) / ADC_BUFFER_LENGTH_HALFWORD;
        uint32_t num_channels = (g_vars.adc_left_channel ? 1 : 0) + (g_vars.adc_right_channel ? 1 : 0);
        uint32_t dur_ms = (used * 1000) / (g_vars.adc_samplerate * num_channels);

        snprintf(resp, sizeof(resp),
            "ADC: %s\r\n"
            "  rate:    %lu Hz\r\n"
            "  channel: %s\r\n"
            "  buffer:  %lu / %lu halfwords (%lu%%, ~%lu ms)\r\n",
            g_vars.adc_running ? "running" : "stopped",
            g_vars.adc_samplerate,
            (g_vars.adc_left_channel && g_vars.adc_right_channel) ? "L+R" :
            (g_vars.adc_left_channel ? "L" : "R"),
            used, (uint32_t)ADC_BUFFER_LENGTH_HALFWORD,
            pct, dur_ms);
        return resp;
    }

    // 未知子命令
    snprintf(resp, sizeof(resp),
        "ADC: unknown subcommand '%s'. Try 'adc -h'\r\n", argv[1]);
    return resp;
}

char* dac_handler(int argc, char **argv)
{
    // 帮助信息
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        snprintf(resp, sizeof(resp),
            "Usage: dac <subcommand> [args]\r\n"
            "Subcommands:\r\n"
            "  start                   Start DAC sine output\r\n"
            "  stop                    Stop DAC output\r\n"
            "  freq [left|right] [Hz]  Set or get sine frequency (Hz)\r\n"
            "  amp  [left|right] [0~1] Set or get sine amplitude (normalized)\r\n"
            "  phase [left|right] [deg] Set or get sine phase (degree)\r\n"
            "  info                    Show DAC configuration and status\r\n"
            "  -h, --help              Show this help message\r\n");
        return resp;
    }

    // dac (无参默认)
    if (argc == 1)
    {
        snprintf(resp, sizeof(resp),
            "DAC: missing subcommand. Try 'dac -h'\r\n");
        return resp;
    }

    // dac start
    if (strcmp(argv[1], "start") == 0)
    {
        if (g_vars.dac_running)
        {
            snprintf(resp, sizeof(resp),
                "DAC: already running, rate=%lu Hz\r\n", g_vars.dac_samplerate);
            return resp;
        }
        g_vars.dac_stream_src = DAC_STREAM_SRC_SINE;
        g_vars.dac_left.phase = 0.0f;
        g_vars.dac_right.phase = 0.0f;
        bsp_dac_start();
        snprintf(resp, sizeof(resp),
            "DAC started, rate=%lu Hz, freq L=%.1f R=%.1f Hz, amp L=%.2f R=%.2f\r\n",
            g_vars.dac_samplerate,
            g_vars.dac_left.frequency, g_vars.dac_right.frequency,
            g_vars.dac_left.amplitude, g_vars.dac_right.amplitude);
        return resp;
    }

    // dac stop
    if (strcmp(argv[1], "stop") == 0)
    {
        if (!g_vars.dac_running)
        {
            snprintf(resp, sizeof(resp), "DAC: already stopped\r\n");
            return resp;
        }
        g_vars.dac_stream_src = DAC_STREAM_SRC_NONE;
        bsp_dac_stop();
        snprintf(resp, sizeof(resp), "DAC stopped\r\n");
        return resp;
    }

    // dac freq
    if (strcmp(argv[1], "freq") == 0)
    {
        if (argc == 2)
        {
            snprintf(resp, sizeof(resp),
                "DAC freq: L=%.1f Hz, R=%.1f Hz\r\n",
                g_vars.dac_left.frequency, g_vars.dac_right.frequency);
            return resp;
        }

        if (argc == 3)
        {
            // dac freq <Hz> → 同时设置左右
            float f = (float)atof(argv[2]);
            if (f <= 0.0f)
            {
                snprintf(resp, sizeof(resp),
                    "DAC freq: invalid value '%s'. Must be > 0 Hz\r\n", argv[2]);
                return resp;
            }
            g_vars.dac_left.frequency = f;
            g_vars.dac_right.frequency = f;
            snprintf(resp, sizeof(resp),
                "DAC freq set to %.1f Hz (both channels)\r\n", f);
            return resp;
        }

        if (argc == 4)
        {
            float f = (float)atof(argv[3]);
            if (f <= 0.0f)
            {
                snprintf(resp, sizeof(resp),
                    "DAC freq: invalid value '%s'. Must be > 0 Hz\r\n", argv[3]);
                return resp;
            }
            if (strcmp(argv[2], "left") == 0)
            {
                g_vars.dac_left.frequency = f;
                snprintf(resp, sizeof(resp),
                    "DAC freq left set to %.1f Hz\r\n", f);
                return resp;
            }
            if (strcmp(argv[2], "right") == 0)
            {
                g_vars.dac_right.frequency = f;
                snprintf(resp, sizeof(resp),
                    "DAC freq right set to %.1f Hz\r\n", f);
                return resp;
            }
            snprintf(resp, sizeof(resp),
                "DAC freq: invalid channel '%s'. Use left or right\r\n", argv[2]);
            return resp;
        }

        snprintf(resp, sizeof(resp),
            "DAC freq: too many arguments. Try 'dac -h'\r\n");
        return resp;
    }

    // dac amp
    if (strcmp(argv[1], "amp") == 0)
    {
        if (argc == 2)
        {
            snprintf(resp, sizeof(resp),
                "DAC amp: L=%.2f, R=%.2f\r\n",
                g_vars.dac_left.amplitude, g_vars.dac_right.amplitude);
            return resp;
        }

        if (argc == 3)
        {
            float a = (float)atof(argv[2]);
            if (a <= 0.0f || a > 1.0f)
            {
                snprintf(resp, sizeof(resp),
                    "DAC amp: invalid value '%s'. Must be 0 < amp <= 1\r\n", argv[2]);
                return resp;
            }
            g_vars.dac_left.amplitude = a;
            g_vars.dac_right.amplitude = a;
            snprintf(resp, sizeof(resp),
                "DAC amp set to %.2f (both channels)\r\n", a);
            return resp;
        }

        if (argc == 4)
        {
            float a = (float)atof(argv[3]);
            if (a <= 0.0f || a > 1.0f)
            {
                snprintf(resp, sizeof(resp),
                    "DAC amp: invalid value '%s'. Must be 0 < amp <= 1\r\n", argv[3]);
                return resp;
            }
            if (strcmp(argv[2], "left") == 0)
            {
                g_vars.dac_left.amplitude = a;
                snprintf(resp, sizeof(resp),
                    "DAC amp left set to %.2f\r\n", a);
                return resp;
            }
            if (strcmp(argv[2], "right") == 0)
            {
                g_vars.dac_right.amplitude = a;
                snprintf(resp, sizeof(resp),
                    "DAC amp right set to %.2f\r\n", a);
                return resp;
            }
            snprintf(resp, sizeof(resp),
                "DAC amp: invalid channel '%s'. Use left or right\r\n", argv[2]);
            return resp;
        }

        snprintf(resp, sizeof(resp),
            "DAC amp: too many arguments. Try 'dac -h'\r\n");
        return resp;
    }

    // dac phase
    if (strcmp(argv[1], "phase") == 0)
    {
        if (argc == 2)
        {
            float deg_l = g_vars.dac_left.phase * 180.0f / (float)M_PI;
            float deg_r = g_vars.dac_right.phase * 180.0f / (float)M_PI;
            snprintf(resp, sizeof(resp),
                "DAC phase: L=%.1f deg, R=%.1f deg\r\n", deg_l, deg_r);
            return resp;
        }

        if (argc == 3)
        {
            float deg = (float)atof(argv[2]);
            float rad = deg * (float)M_PI / 180.0f;
            g_vars.dac_left.phase = rad;
            g_vars.dac_right.phase = rad;
            snprintf(resp, sizeof(resp),
                "DAC phase set to %.1f deg (both channels)\r\n", deg);
            return resp;
        }

        if (argc == 4)
        {
            float deg = (float)atof(argv[3]);
            float rad = deg * (float)M_PI / 180.0f;
            if (strcmp(argv[2], "left") == 0)
            {
                g_vars.dac_left.phase = rad;
                snprintf(resp, sizeof(resp),
                    "DAC phase left set to %.1f deg\r\n", deg);
                return resp;
            }
            if (strcmp(argv[2], "right") == 0)
            {
                g_vars.dac_right.phase = rad;
                snprintf(resp, sizeof(resp),
                    "DAC phase right set to %.1f deg\r\n", deg);
                return resp;
            }
            snprintf(resp, sizeof(resp),
                "DAC phase: invalid channel '%s'. Use left or right\r\n", argv[2]);
            return resp;
        }

        snprintf(resp, sizeof(resp),
            "DAC phase: too many arguments. Try 'dac -h'\r\n");
        return resp;
    }

    // dac info
    if (strcmp(argv[1], "info") == 0)
    {
        uint32_t in  = g_vars.dac_buffer_input_index;
        uint32_t out = g_vars.dac_buffer_output_index;
        uint32_t used;
        if (in >= out)
            used = in - out;
        else
            used = DAC_BUFFER_LENGTH_HALFWORD - out + in;

        uint32_t pct = (used * 100) / DAC_BUFFER_LENGTH_HALFWORD;
        uint32_t dur_ms = (used * 1000) / (g_vars.dac_samplerate * 2);

        snprintf(resp, sizeof(resp),
            "DAC: %s\r\n"
            "  src:     %s\r\n"
            "  rate:    %lu Hz\r\n"
            "  freq:    L=%.1f Hz, R=%.1f Hz\r\n"
            "  amp:     L=%.2f, R=%.2f\r\n"
            "  phase:   L=%.1f deg, R=%.1f deg\r\n"
            "  buffer:  %lu / %lu halfwords (%lu%%, ~%lu ms)\r\n",
            g_vars.dac_running ? "running" : "stopped",
            g_vars.dac_stream_src == DAC_STREAM_SRC_SINE ? "sine" : "none",
            g_vars.dac_samplerate,
            g_vars.dac_left.frequency, g_vars.dac_right.frequency,
            g_vars.dac_left.amplitude, g_vars.dac_right.amplitude,
            g_vars.dac_left.phase * 180.0f / (float)M_PI,
            g_vars.dac_right.phase * 180.0f / (float)M_PI,
            used, (uint32_t)DAC_BUFFER_LENGTH_HALFWORD,
            pct, dur_ms);
        return resp;
    }

    // 未知子命令
    snprintf(resp, sizeof(resp),
        "DAC: unknown subcommand '%s'. Try 'dac -h'\r\n", argv[1]);
    return resp;
}
