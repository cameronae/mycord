#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <ctype.h>

/* ==================== Internal helpers ==================== */

/* ==================== Color Registry ==================== */
#define MAX_TRACKED_USERS 64

typedef struct {
    char username[TUI_USERNAME_LEN];
    const char* color;
} user_color_map_t;

static user_color_map_t g_user_colors[MAX_TRACKED_USERS];
static int g_tracked_user_count = 0;

static const char* get_user_color(const char* username) {
    // A diverse palette of distinct colors (excluding Green/Red)
    static const char* palette[] = {
        "\033[36m",   // Cyan
        "\033[33m",   // Yellow
        "\033[94m",   // Light Blue
        "\033[35m",   // Magenta
        "\033[96m",   // Light Cyan
        "\033[95m",   // Light Magenta
        "\033[34m"    // Blue
    };
    int num_colors = sizeof(palette) / sizeof(palette[0]);

    // 1. Check if we already assigned this user a color
    for (int i = 0; i < g_tracked_user_count; i++) {
        if (strcmp(g_user_colors[i].username, username) == 0) {
            return g_user_colors[i].color; // Return saved color
        }
    }

    // 2. If new user, assign the next sequential color from the palette
    const char* assigned_color = palette[g_tracked_user_count % num_colors];
    
    // 3. Save it to the registry (if we have room)
    if (g_tracked_user_count < MAX_TRACKED_USERS) {
        strncpy(g_user_colors[g_tracked_user_count].username, username, TUI_USERNAME_LEN - 1);
        g_user_colors[g_tracked_user_count].username[TUI_USERNAME_LEN - 1] = '\0';
        g_user_colors[g_tracked_user_count].color = assigned_color;
        g_tracked_user_count++;
    }

    return assigned_color;
}

// Move cursor to absolute (row, col) — both 1-indexed
static void cur_move(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

// Clear from cursor to end of line
static void cur_clear_eol(void) {
    printf(ANSI_CLEAR_LINE);
}

// Print a horizontal rule of box-drawing chars, width w
static void draw_hline(int w, const char* color) {
    printf("%s", color);
    for (int i = 0; i < w; i++) printf(BOX_H);
    printf(ANSI_RESET);
}

// Clamp scroll_offset to valid range
static void clamp_scroll(tui_state_t* tui) {
    int msg_area_rows = tui->rows - 4;  // header(2) + divider(1) + input(1) + bottom border(1) = 5? let's compute
    // Layout: row1=top border, row2=header, row3=divider, row4..rows-2=messages, rows-1=input divider, rows=input
    // Message area: rows - 5 lines tall  (border + header + divider + input_div + input)
    if (msg_area_rows < 1) msg_area_rows = 1;

    int max_scroll = tui->msg_count - msg_area_rows;
    if (max_scroll < 0) max_scroll = 0;

    if (tui->scroll_offset > max_scroll) tui->scroll_offset = max_scroll;
    if (tui->scroll_offset < 0)          tui->scroll_offset = 0;
}

// Get terminal size
static void get_term_size(int* rows, int* cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

/* ==================== Layout constants ==================== */
// Given tui->rows and tui->cols:
//   Row 1              : top border  ╭───...───╮
//   Row 2              : header bar  │ MYCORD  ·  username  ● CONNECTED │
//   Row 3              : divider     ├───...───┤
//   Rows 4..rows-2     : message area (scrollable)
//   Row rows-1         : input divider  ├───...───┤
//   Row rows           : input line  │ ▸ <input>            cursor │

static inline int msg_area_top(tui_state_t* t)    { (void)t; return 4; }
static inline int msg_area_bottom(tui_state_t* t)  { return t->rows - 2; }
static inline int msg_area_height(tui_state_t* t)  { return msg_area_bottom(t) - msg_area_top(t) + 1; }
static inline int input_row(tui_state_t* t)        { return t->rows; }
static inline int inner_width(tui_state_t* t)      { return t->cols - 2; }  // inside the │ borders

/* ==================== Draw regions ==================== */

static void draw_top_border(tui_state_t* tui) {
    cur_move(1, 1);
    printf("%s%s", C_BORDER, BOX_TL);
    draw_hline(inner_width(tui), C_BORDER);
    printf("%s%s%s", C_BORDER, BOX_TR, ANSI_RESET);
}

static void draw_header(tui_state_t* tui) {
    cur_move(2, 1);
    printf("%s%s%s", C_BORDER, BOX_V, ANSI_RESET);

    // Left: logo
    printf(" %s%s✦ MYCORD%s", ANSI_BOLD, C_HEADER_FG, ANSI_RESET);

    // Center spacer + right: username + status
    const char* status_color = tui->connected ? C_STATUS_OK : C_STATUS_ERR;
    const char* status_dot   = tui->connected ? CIRCLE : CIRCLE;
    const char* status_text  = tui->connected ? "CONNECTED" : "DISCONNECTED";

    // Build right section string (estimate length)
    char right[128];
    int rlen = snprintf(right, sizeof(right), " %s %s%s%s %s",
                        tui->username,
                        status_color, status_dot, ANSI_RESET,
                        status_text);
    // Visible length (strip ANSI for padding calculation)
    int visible_right = (int)strlen(tui->username) + 1 + 1 + 1 + 1 + (int)strlen(status_text);
    // Left visible: " ✦ MYCORD" = 9 visible chars (space + ✦ + space + MYCORD)
    int visible_left = 9;
    int total_inner = inner_width(tui);
    int spaces = total_inner - visible_left - visible_right - 1;
    if (spaces < 1) spaces = 1;

    printf("%s%*s%s%s", C_HEADER_DIM, spaces, "", ANSI_RESET, right);

    // Pad to end
    // Compute remaining space
    int used = visible_left + spaces + visible_right;
    int remaining = total_inner - used;
    if (remaining > 0) printf("%*s", remaining, "");

    printf("%s%s%s", C_BORDER, BOX_V, ANSI_RESET);
}

static void draw_top_divider(tui_state_t* tui) {
    cur_move(3, 1);
    printf("%s%s", C_BORDER, BOX_ML);
    draw_hline(inner_width(tui), C_BORDER);
    printf("%s%s%s", C_BORDER, BOX_MR, ANSI_RESET);
}

static void draw_bottom_border(tui_state_t* tui) {
    cur_move(tui->rows - 1, 1);
    printf("%s%s", C_BORDER, BOX_ML);
    draw_hline(inner_width(tui), C_BORDER);
    printf("%s%s%s", C_BORDER, BOX_MR, ANSI_RESET);
}

// Render a single message line into the message area at screen row `row`
static void draw_message_line(tui_state_t* tui, int row, int msg_idx) {
    cur_move(row, 1);
    printf("%s%s%s", C_BORDER, BOX_V, ANSI_RESET);

    int w = inner_width(tui);
    int written = 0;  // visible chars written so far

    if (msg_idx < 0 || msg_idx >= tui->msg_count) {
        // Empty line
        printf("%*s", w, "");
        printf("%s%s%s", C_BORDER, BOX_V, ANSI_RESET);
        return;
    }

    tui_message_t* m = &tui->messages[msg_idx];

    switch (m->kind) {
        case TUI_MSG_SYSTEM:
            printf(" %s%s %s%s", C_SYSTEM, DOT, m->text, ANSI_RESET);
            written = 1 + 1 + 1 + (int)strlen(m->text);
            break;

        case TUI_MSG_DISCONNECT:
            printf(" %s%s %s%s", C_DISCONNECT, BULLET, m->text, ANSI_RESET);
            written = 1 + 1 + 1 + (int)strlen(m->text);
            break;

        case TUI_MSG_CHAT:
        case TUI_MSG_OWN: {
            // timestamp
            printf(" %s%s%s", C_MSG_TIME, m->timestamp, ANSI_RESET);
            written += 1 + (int)strlen(m->timestamp);

            // username (own = green, others = dynamic hash)
            const char* uc;
            if (m->kind == TUI_MSG_OWN) {
                uc = C_MSG_SELF; // Keep your original green for yourself
            } else {
                uc = get_user_color(m->username); // Pick dynamic color
            }
            
            printf(" %s%s%s%s%s:", uc, ANSI_BOLD, m->username, ANSI_RESET, C_BORDER);
            written += 1 + (int)strlen(m->username) + 1;  // space + name + colon

            printf("%s", ANSI_RESET);

            // message text — highlight @username mentions
            if (!tui->quiet) {
                char mention[TUI_USERNAME_LEN + 2];
                snprintf(mention, sizeof(mention), "@%s", tui->username);
                const char* pos = m->text;
                const char* hit;
                while ((hit = strstr(pos, mention)) != NULL) {
                    int pre = (int)(hit - pos);
                    printf(" %s%.*s%s", C_MSG_TEXT, pre, pos, ANSI_RESET);
                    written += 1 + pre;
                    printf("\a%s%s%s%s", ANSI_BOLD, C_MENTION, mention, ANSI_RESET);
                    written += (int)strlen(mention);
                    pos = hit + strlen(mention);
                }
                printf(" %s%s%s", C_MSG_TEXT, pos, ANSI_RESET);
                written += 1 + (int)strlen(pos);
            } else {
                printf(" %s%s%s", C_MSG_TEXT, m->text, ANSI_RESET);
                written += 1 + (int)strlen(m->text);
            }
            break;
        }
    }

    // Pad to fill the inner width
    int pad = w - written;
    if (pad > 0 && pad < 4096) printf("%*s", pad, "");

    printf("%s%s%s", C_BORDER, BOX_V, ANSI_RESET);
}

static void draw_message_area(tui_state_t* tui) {
    int height = msg_area_height(tui);
    int top    = msg_area_top(tui);

    // Determine which messages to show.
    // scroll_offset=0 → show the newest `height` messages.
    // scroll_offset=N → show messages ending at msg_count-1-N.
    int last_visible = tui->msg_count - 1 - tui->scroll_offset;  // newest shown
    int first_msg_idx = last_visible - height + 1;

    for (int line = 0; line < height; line++) {
        int msg_idx = first_msg_idx + line;
        draw_message_line(tui, top + line, msg_idx);
    }

    // Scroll indicator (top-right of message area when scrolled)
    if (tui->scroll_offset > 0) {
        cur_move(top, tui->cols - 12);
        printf("%s↑ -%d %s", C_SCROLL_INFO, tui->scroll_offset, ANSI_RESET);
    }
}

static void draw_input(tui_state_t* tui) {
    cur_move(input_row(tui), 1);
    printf("%s%s%s", C_BORDER, BOX_V, ANSI_RESET);

    int w = inner_width(tui);
    // Label
    printf(" %s%s%s%s ", C_INPUT_LABEL, ANSI_BOLD, BULLET, ANSI_RESET);
    int label_len = 3;  // " ▸ "

    // Available space for text
    int text_w = w - label_len - 1;  // -1 for trailing space before border
    if (text_w < 1) text_w = 1;

    // If cursor is near the end, show the tail of the buffer; otherwise show from start
    const char* buf = tui->input;
    int len = tui->input_len;
    int cur = tui->cursor_pos;

    // Compute a viewport window
    int view_start = 0;
    if (cur >= text_w) {
        view_start = cur - text_w + 1;
    }

    int visible = len - view_start;
    if (visible > text_w) visible = text_w;

    printf("%s%s%.*s%s", C_INPUT_TEXT, ANSI_BOLD, visible, buf + view_start, ANSI_RESET);

    // Pad
    int pad = text_w - visible;
    if (pad > 0 && pad < 4096) printf("%*s", pad, "");

    printf(" %s%s%s", C_BORDER, BOX_V, ANSI_RESET);

    // Restore cursor to the actual cursor position within the input
    int cursor_screen_col = 1 + 1 + label_len + (cur - view_start) + 1;  // border + space + label + offset
    if (cursor_screen_col < 3) cursor_screen_col = 3;
    if (cursor_screen_col > tui->cols - 1) cursor_screen_col = tui->cols - 1;
    cur_move(input_row(tui), cursor_screen_col);
}

/* ==================== Public API ==================== */

void tui_init(tui_state_t* tui, const char* username, bool quiet) {
    memset(tui, 0, sizeof(*tui));
    strncpy(tui->username, username, TUI_USERNAME_LEN - 1);
    tui->connected = true;
    tui->quiet = quiet;

    get_term_size(&tui->rows, &tui->cols);

    // Enter alternate screen, hide cursor
    printf(ANSI_ALT_BUF_ON);
    printf(ANSI_HIDE_CURSOR);
    printf(ANSI_CLEAR);
    fflush(stdout);

    tui_draw(tui);
}

void tui_cleanup(tui_state_t* tui) {
    (void)tui;
    printf(ANSI_SHOW_CURSOR);
    printf(ANSI_ALT_BUF_OFF);
    fflush(stdout);
}

void tui_resize(tui_state_t* tui) {
    get_term_size(&tui->rows, &tui->cols);
    if (tui->rows < TUI_MIN_ROWS) tui->rows = TUI_MIN_ROWS;
    if (tui->cols < TUI_MIN_COLS) tui->cols = TUI_MIN_COLS;
    clamp_scroll(tui);
    printf(ANSI_CLEAR);
    tui_draw(tui);
}

void tui_draw(tui_state_t* tui) {
    printf(ANSI_HIDE_CURSOR);

    draw_top_border(tui);
    draw_header(tui);
    draw_top_divider(tui);
    draw_message_area(tui);
    draw_bottom_border(tui);
    draw_input(tui);

    printf(ANSI_SHOW_CURSOR);
    fflush(stdout);
}

void tui_add_message(tui_state_t* tui,
                     tui_msg_kind_t kind,
                     const char* timestamp,
                     const char* username,
                     const char* text)
{
    if (tui->msg_count >= TUI_MAX_MESSAGES) {
        // Shift out oldest message
        memmove(&tui->messages[0], &tui->messages[1],
                sizeof(tui_message_t) * (TUI_MAX_MESSAGES - 1));
        tui->msg_count = TUI_MAX_MESSAGES - 1;
    }

    tui_message_t* m = &tui->messages[tui->msg_count++];
    m->kind = kind;
    strncpy(m->timestamp, timestamp ? timestamp : "", sizeof(m->timestamp) - 1);
    strncpy(m->username,  username  ? username  : "", sizeof(m->username) - 1);
    strncpy(m->text,      text      ? text      : "", sizeof(m->text) - 1);

    // If user has scrolled up, keep their position; otherwise stay at bottom
    clamp_scroll(tui);

    tui_draw(tui);
}

const char* tui_handle_key(tui_state_t* tui, int ch) {
    static char result[TUI_INPUT_MAX + 1];

    switch (ch) {
        case '\n':
        case '\r': {
            // Return completed line
            if (tui->input_len == 0) return NULL;
            strncpy(result, tui->input, TUI_INPUT_MAX);
            result[tui->input_len] = '\0';
            // Clear input buffer
            memset(tui->input, 0, sizeof(tui->input));
            tui->input_len = 0;
            tui->cursor_pos = 0;
            tui_draw(tui);
            return result;
        }

        case 127:   // DEL / Backspace
        case '\b': {
            if (tui->cursor_pos > 0) {
                // Remove char before cursor
                memmove(&tui->input[tui->cursor_pos - 1],
                        &tui->input[tui->cursor_pos],
                        tui->input_len - tui->cursor_pos);
                tui->input_len--;
                tui->cursor_pos--;
                tui->input[tui->input_len] = '\0';
                tui_draw(tui);
            }
            break;
        }

        case 1: {  // Ctrl+A — move to start
            tui->cursor_pos = 0;
            tui_draw(tui);
            break;
        }
        case 5: {  // Ctrl+E — move to end
            tui->cursor_pos = tui->input_len;
            tui_draw(tui);
            break;
        }
        case 11: {  // Ctrl+K — kill to end of line
            tui->input_len = tui->cursor_pos;
            tui->input[tui->input_len] = '\0';
            tui_draw(tui);
            break;
        }
        case 21: {  // Ctrl+U — kill entire line
            memset(tui->input, 0, sizeof(tui->input));
            tui->input_len = 0;
            tui->cursor_pos = 0;
            tui_draw(tui);
            break;
        }

        default: {
            // Printable character — insert at cursor position
            if (isprint(ch) && tui->input_len < TUI_INPUT_MAX) {
                memmove(&tui->input[tui->cursor_pos + 1],
                        &tui->input[tui->cursor_pos],
                        tui->input_len - tui->cursor_pos);
                tui->input[tui->cursor_pos] = (char)ch;
                tui->input_len++;
                tui->cursor_pos++;
                tui->input[tui->input_len] = '\0';
                tui_draw(tui);
            }
            break;
        }
    }
    return NULL;
}

void tui_handle_escape(tui_state_t* tui, char final_byte) {
    switch (final_byte) {
        case 'A':  // Up arrow — scroll up one line
            tui->scroll_offset++;
            clamp_scroll(tui);
            tui_draw(tui);
            break;

        case 'B':  // Down arrow — scroll down one line
            if (tui->scroll_offset > 0) {
                tui->scroll_offset--;
                clamp_scroll(tui);
                tui_draw(tui);
            }
            break;

        case 'C':  // Right arrow — move cursor right
            if (tui->cursor_pos < tui->input_len) {
                tui->cursor_pos++;
                tui_draw(tui);
            }
            break;

        case 'D':  // Left arrow — move cursor left
            if (tui->cursor_pos > 0) {
                tui->cursor_pos--;
                tui_draw(tui);
            }
            break;

        case '5':  // Page Up — scroll up by page
            tui->scroll_offset += msg_area_height(tui);
            clamp_scroll(tui);
            tui_draw(tui);
            break;

        case '6':  // Page Down — scroll down by page
            tui->scroll_offset -= msg_area_height(tui);
            if (tui->scroll_offset < 0) tui->scroll_offset = 0;
            tui_draw(tui);
            break;
    }
}

void tui_set_disconnected(tui_state_t* tui) {
    tui->connected = false;
    tui_draw(tui);
}