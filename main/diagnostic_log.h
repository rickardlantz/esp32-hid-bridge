#pragma once
#include <stdbool.h>

void diagnostic_log_init(void);
void diagnostic_log_capture(const char *line);
void diagnostic_log_enable(bool enabled);
void diagnostic_log_dump(void);
void diagnostic_log_clear(void);
