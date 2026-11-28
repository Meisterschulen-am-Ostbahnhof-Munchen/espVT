#pragma once

#include <cstdint>

#include "esp_err.h"

// Touch controller of the selected board. Touches are delivered in LCD
// coordinates to isovt::on_touch_pressed()/on_touch_released().
namespace touch
{
// The touch task runs a pressed object's macros through the VT server
// (isovt::on_touch_pressed()/on_touch_released() -> process_macro()). A macro
// command such as Change Active Mask runs
// VirtualTerminalServer::process_connection_dependent_messages(), whose stack
// frame alone is about 7 KB, so 4 KB overflowed.
constexpr std::uint32_t TASK_STACK_SIZE = 16384;

esp_err_t start();
bool is_ready();
}
