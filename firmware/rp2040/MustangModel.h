#pragma once

#include <Arduino.h>
#include "mcp_can.h"

void mustang_init(MCP_CAN* can);
void mustang_tick(unsigned long now);
void mustang_handleCommand(String* tokens, int count);
