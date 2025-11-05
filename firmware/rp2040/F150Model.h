#pragma once

#include <Arduino.h>
#include "mcp_can.h"

void f150_init(MCP_CAN* can);
void f150_tick(unsigned long now);
void f150_handleCommand(String* tokens, int count);
