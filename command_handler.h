#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <stdint.h>

char* lscmd_handler(int argc, char **argv);

char* echo_handler(int argc, char **argv);

char* adc_handler(int argc, char **argv);

char* dac_handler(int argc, char **argv);

#endif // COMMAND_HANDLER_H
