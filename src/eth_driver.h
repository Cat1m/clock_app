#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void        eth_driver_init(void);
bool        eth_driver_got_ip(void);
const char *eth_driver_ip_str(void);

#ifdef __cplusplus
}
#endif
