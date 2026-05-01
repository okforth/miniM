// Defines ANSI escape sequences used in our program

// Refreshes screen and moves cursor to top-left (default) position:
#define FULL_SCREEN_REFRESH "\e[2J\e[H" 
// Moves cursor 2 lines up, refreshes from cursor to beginning of terminal position (X: 1, Y: 1) and places it there:
#define REFRESH_ABOVE_STATUS_BAR "\e[2A\e[1J\e[H"
#define REFRESH_BELOW_CURSOR "\e[0J"
// Moves cursor to position (1,1):
#define CURSOR_DEFAULT_POS "\e[H"
#define CURSOR_JUMP_1_LINE_UP "\e[1A"
#define CURSOR_JUMP_1_LINE_DOWN "\e[1B"
#define CURSOR_MOVE_1_COL_LEFT "\e[1D"
#define REFRESH_ENTIRE_LINE "\e[2K"
// Erases from cursor to end of line:
#define REFRESH_TIL_LINE_END "\e[0K"
#define HIDE_CURSOR "\e[?25l"
#define SHOW_CURSOR "\e[?25h"
#define SAVE_CURSOR_POS "\e7"
#define RESTORE_CURSOR_POS "\e8"
#define CURSOR_MOVE_ROW(row) printf("\e[%d;1H", (row))
#define CURSOR_MOVE_POS(row, col) printf("\e[%d;%dH", (row), (col))

// Defines below print help/"tutorial" info on the status bar (bottom of the screen)

// witout formatting:        "---\r\nCTRL+S to save to the file | CTRL+O to open a file | CTRL+N to open new file | CTRL+Q to exit the program | CTRL+T to change input mode"
#define STATUS_BAR_TEXT_LONG "---\r\n\e[1m\e[3mCTRL+S\e[0m to save to the file | \e[1m\e[3mCTRL+O\e[0m to open a file | \e[1m\e[3mCTRL+N\e[0m to open new file | \e[1m\e[3mCTRL+Q\e[0m to exit the program | \e[1m\e[3mCTRL+T\e[0m to change input mode"
// - || -               "---\r\nCTRL+S to save | CTRL+O to open file | CTRL+N to open new | CTRL+Q to exit | CTRL+T to change mode"
#define STATUS_BAR_TEXT "---\r\n\e[1m\e[3mCTRL+S\e[0m to save | \e[1m\e[3mCTRL+O\e[0m to open file | \e[1m\e[3mCTRL+N\e[0m to open new | \e[1m\e[3mCTRL+Q\e[0m to exit | \e[1m\e[3mCTRL+T\e[0m to change mode"
// - || -                     "---\r\nCTRL+S save | CTRL+O open | CTRL+N new | CTRL+Q exit | CTRL+T mode"
#define STATUS_BAR_TEXT_SHORT "---\r\n\e[1m\e[3mCTRL+S\e[0m save | \e[1m\e[3mCTRL+O\e[0m open | \e[1m\e[3mCTRL+N\e[0m new | \e[1m\e[3mCTRL+Q\e[0m exit | \e[1m\e[3mCTRL+T\e[0m mode"