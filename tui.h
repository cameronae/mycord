#ifndef TUI_H
#define TUI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ==================== TUI Constants ==================== */

// ANSI escape sequences
#define ANSI_RESET       "\033[0m"
#define ANSI_BOLD        "\033[1m"
#define ANSI_DIM         "\033[2m"
#define ANSI_CLEAR       "\033[2J"
#define ANSI_CLEAR_LINE  "\033[2K"
#define ANSI_HOME        "\033[H"
#define ANSI_HIDE_CURSOR "\033[?25l"
#define ANSI_SHOW_CURSOR "\033[?25h"
#define ANSI_ALT_BUF_ON  "\033[?1049h"
#define ANSI_ALT_BUF_OFF "\033[?1049l"
#define ANSI_SAVE_CUR    "\033[s"
#define ANSI_RESTORE_CUR "\033[u"

// Colors — retro-terminal aesthetic: dark bg, amber/green accents
#define C_BG_DEFAULT     ""                // transparent (terminal bg)
#define C_BORDER         "\033[38;5;240m"  // dim gray border
#define C_HEADER_BG      "\033[48;5;234m"  // near-black header bg
#define C_HEADER_FG      "\033[38;5;214m"  // amber title
#define C_HEADER_DIM     "\033[38;5;244m"  // dim gray subtitle
#define C_MSG_TIME       "\033[38;5;240m"  // very dim gray timestamp
#define C_MSG_USER       "\033[38;5;214m"  // amber username
#define C_MSG_SELF       "\033[38;5;77m"   // green for own messages
#define C_MSG_TEXT       "\033[38;5;252m"  // near-white message text
#define C_MENTION        "\033[38;5;196m"  // bright red mentions
#define C_SYSTEM         "\033[38;5;244m"  // mid-gray system messages
#define C_DISCONNECT     "\033[38;5;196m"  // red disconnects
#define C_INPUT_BG       "\033[48;5;234m"  // dark input background
#define C_INPUT_LABEL    "\033[38;5;214m"  // amber "you:" label
#define C_INPUT_TEXT     "\033[38;5;255m"  // bright white input text
#define C_SCROLL_INFO    "\033[38;5;238m"  // very dim scroll indicator
#define C_STATUS_OK      "\033[38;5;77m"   // green connected indicator
#define C_STATUS_ERR     "\033[38;5;196m"  // red disconnected indicator
#define C_DIVIDER        "\033[38;5;236m"  // subtle divider line

// Box-drawing characters (UTF-8)
#define BOX_H   "─"
#define BOX_V   "│"
#define BOX_TL  "╭"
#define BOX_TR  "╮"
#define BOX_BL  "╰"
#define BOX_BR  "╯"
#define BOX_ML  "├"
#define BOX_MR  "┤"
#define DOT     "·"
#define BULLET  "▸"
#define CIRCLE  "●"

#define TUI_MAX_MESSAGES   500
#define TUI_MSG_LINE_LEN   512   // max rendered line length
#define TUI_INPUT_MAX      1023  // max input buffer length
#define TUI_USERNAME_LEN   32
#define TUI_MIN_ROWS       10
#define TUI_MIN_COLS       40

/* ==================== Structs ==================== */

typedef enum {
    TUI_MSG_CHAT = 0,
    TUI_MSG_SYSTEM,
    TUI_MSG_DISCONNECT,
    TUI_MSG_OWN       // echoed back or locally generated own messages
} tui_msg_kind_t;

typedef struct {
    tui_msg_kind_t kind;
    char timestamp[20];   // "HH:MM:SS"
    char username[TUI_USERNAME_LEN];
    char text[TUI_MSG_LINE_LEN];
} tui_message_t;

typedef struct {
    // Terminal dimensions
    int rows;
    int cols;

    // Message store
    tui_message_t messages[TUI_MAX_MESSAGES];
    int msg_count;

    // Scroll offset: 0 = bottom (newest), positive = scrolled up
    int scroll_offset;

    // Input buffer
    char input[TUI_INPUT_MAX + 1];
    int  input_len;
    int  cursor_pos;   // cursor position within input (0..input_len)

    // Own username (for mention highlight + coloring own messages)
    char username[TUI_USERNAME_LEN];

    // Connection state for status indicator
    bool connected;

    // Quiet mode flag (suppress mention highlights/bell)
    bool quiet;
} tui_state_t;

/* ==================== API ==================== */

//hash function for colors
static const char* get_user_color(const char* username);

/**
 * Initialize TUI: enter alternate screen buffer, hide cursor, get term size.
 * Must be called before any other tui_ function.
 */
void tui_init(tui_state_t* tui, const char* username, bool quiet);

/**
 * Tear down TUI: restore terminal, show cursor, leave alternate buffer.
 */
void tui_cleanup(tui_state_t* tui);

/**
 * Re-query terminal size and schedule a full redraw.
 */
void tui_resize(tui_state_t* tui);

/**
 * Full redraw of all UI regions.
 */
void tui_draw(tui_state_t* tui);

/**
 * Add a chat message and redraw.
 */
void tui_add_message(tui_state_t* tui,
                     tui_msg_kind_t kind,
                     const char* timestamp,
                     const char* username,
                     const char* text);

/**
 * Handle a single raw keypress byte.
 * Escape sequences (arrow keys etc.) must be assembled by the caller
 * using tui_handle_escape().
 * Returns the completed input line (NUL-terminated, static storage)
 * when Enter is pressed, otherwise NULL.
 */
const char* tui_handle_key(tui_state_t* tui, int ch);

/**
 * Handle an escape sequence already parsed into a CSI final byte + params.
 * E.g. for Up arrow: final='A', params="".
 */
void tui_handle_escape(tui_state_t* tui, char final_byte);

/**
 * Mark connection as lost and redraw status bar.
 */
void tui_set_disconnected(tui_state_t* tui);

#endif // TUI_H