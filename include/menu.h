// menu.h - Interactive text menu front-end.
//
// A simple numbered-menu wrapper around CommandInterpreter, for users who
// prefer guided prompts over typing commands. Every action is implemented by
// dispatching the equivalent interpreter command, so behaviour matches scripts
// and the command shell exactly.
#ifndef FAN_TOOL_MENU_H
#define FAN_TOOL_MENU_H

#include <iosfwd>

#include "command_interpreter.h"

namespace fan {

// Run the text menu loop, reading choices from `in` and writing to `out`.
// Returns when the user selects Quit (or on EOF).
void run_menu(CommandInterpreter& interp, std::istream& in, std::ostream& out);

}  // namespace fan

#endif  // FAN_TOOL_MENU_H
