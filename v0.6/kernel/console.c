#include "console.h"
#include "fat12.h"
#include "netfs.h"
#include "time.h"
#include <stdint.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_ADDR    0xB8000

#define VGA_BLACK         0x0
#define VGA_BLUE          0x1
#define VGA_GREEN         0x2
#define VGA_CYAN          0x3
#define VGA_RED           0x4
#define VGA_MAGENTA       0x5
#define VGA_BROWN         0x6
#define VGA_LIGHT_GREY    0x7
#define VGA_DARK_GREY     0x8
#define VGA_LIGHT_BLUE    0x9
#define VGA_LIGHT_GREEN   0xA
#define VGA_LIGHT_CYAN    0xB
#define VGA_LIGHT_RED     0xC
#define VGA_LIGHT_MAGENTA 0xD
#define VGA_YELLOW        0xE
#define VGA_WHITE         0xF

#define VGA_COLOR(fg, bg)  ((fg) | ((bg) << 4))
#define COLOR_NORMAL       VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLACK)
#define COLOR_PROMPT       VGA_COLOR(VGA_WHITE,       VGA_BLACK)
#define COLOR_ERROR        VGA_COLOR(VGA_LIGHT_RED,   VGA_BLACK)
#define COLOR_WARNING      VGA_COLOR(VGA_YELLOW,      VGA_BLACK)
#define COLOR_BACKGROUND   VGA_COLOR(VGA_WHITE,       VGA_BLUE)

static volatile uint8_t* vga = (volatile uint8_t*)VGA_ADDR;

static int     cursor_row    = 0;
static int     cursor_col    = 0;
static uint8_t current_color = COLOR_NORMAL;

static char    input_buf[256];
static int     input_len = 0;

static const char* PROMPT = "MyOS> ";

static uint8_t file_buf[32 * 1024];    /* shared I/O buffer          */
static uint8_t script_buf[4 * 1024];   /* kept separate from file_buf */

static int      write_mode = 0;
static char     write_name[32];
static uint32_t write_len  = 0;

#define WRITE_BUF_SIZE (32 * 1024)

/* ── VGA helpers ─────────────────────────────────────────────────────────── */

void console_set_color(uint8_t color) { current_color = color; }

static void set_cell(int row, int col, char c, uint8_t color) {
    int pos = (row * VGA_WIDTH + col) * 2;
    vga[pos]     = (uint8_t)c;
    vga[pos + 1] = color;
}

static void clear_screen(void) {
    for (int r = 0; r < VGA_HEIGHT; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            set_cell(r, c, ' ', COLOR_NORMAL);
    cursor_row = 0;
    cursor_col = 0;
}

static void scroll_up(void) {
    for (int r = 0; r < VGA_HEIGHT - 1; r++)
        for (int c = 0; c < VGA_WIDTH; c++) {
            int dst = (r * VGA_WIDTH + c) * 2;
            int src = ((r+1) * VGA_WIDTH + c) * 2;
            vga[dst]     = vga[src];
            vga[dst + 1] = vga[src + 1];
        }
    for (int c = 0; c < VGA_WIDTH; c++)
        set_cell(VGA_HEIGHT - 1, c, ' ', COLOR_NORMAL);
    cursor_row = VGA_HEIGHT - 1;
    cursor_col = 0;
}

static void advance_cursor(void) {
    if (++cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        if (++cursor_row >= VGA_HEIGHT) scroll_up();
    }
}

/* ── Public output ───────────────────────────────────────────────────────── */

void console_putchar(char c) {
    if (c == '\n') {
        cursor_col = 0;
        if (++cursor_row >= VGA_HEIGHT) scroll_up();
        return;
    }
    set_cell(cursor_row, cursor_col, c, current_color);
    advance_cursor();
}

void console_print(const char* s)  { while (*s) console_putchar(*s++); }
void console_newline(void)          { console_putchar('\n'); }

/* ── Numeric helper ──────────────────────────────────────────────────────── */

static void print_uint(uint32_t n) {
    if (n == 0) { console_putchar('0'); return; }
    char tmp[12]; int i = 0;
    while (n) { tmp[i++] = '0' + (n % 10); n /= 10; }
    while (i)  console_putchar(tmp[--i]);
}

/* ── String helpers ──────────────────────────────────────────────────────── */

static int str_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static int str_starts(const char* s, const char* p) {
    while (*p) { if (*s != *p) return 0; s++; p++; }
    return 1;
}

static const char* skip_spaces(const char* s) {
    while (*s == ' ') s++;
    return s;
}

/* ── Prompt ──────────────────────────────────────────────────────────────── */

static void print_prompt(void) {
    const char* p = PROMPT;
    while (*p) { set_cell(cursor_row, cursor_col, *p++, COLOR_PROMPT); advance_cursor(); }
}

/* ── ls callback ─────────────────────────────────────────────────────────── */

static void ls_callback(const FAT12_FileInfo* info) {
    console_print(info->name);
    if (info->ext[0]) { console_putchar('.'); console_print(info->ext); }
    int n = 0;
    for (const char* p = info->name; *p; p++) n++;
    if (info->ext[0]) { n++; for (const char* p = info->ext; *p; p++) n++; }
    while (n < 16) { console_putchar(' '); n++; }
    if (info->attributes & FAT_ATTR_DIRECTORY)
        console_print("<DIR>          ");
    else { print_uint(info->file_size); console_print(" bytes"); }
    console_newline();
}

/* ── Command implementations ─────────────────────────────────────────────── */

static void cmd_help(void) {
    console_print("Available commands:\n");
    console_print("  help           - show this message\n");
    console_print("  help scripts   - how to write script files\n");
    console_print("  clear          - clear the screen\n");
    console_print("  ver            - show OS version\n");
    console_print("  ls             - list files on FAT12 disk\n");
    console_print("  cat <file>     - print a text file to screen\n");
    console_print("  read <file>    - same as cat\n");
    console_print("  get <file>     - fetch file from network server\n");
    console_print("  netls          - list files on network server\n");
    console_print("  write <file>   - write a text file and send to server\n");
    console_print("  run <file>     - run a script file\n");
    console_print("  time           - show the current time\n");
}

static void cmd_help_scripts(void) {
    console_print("── Script files ─────────────────────────\n");
    console_print("Scripts are plain text files on the local\n");
    console_print("FAT12 disk. Each line is one command.\n");
    console_print("\n");
    console_print("RULES:\n");
    console_print("  # starts a comment (line is skipped)\n");
    console_print("  Blank lines are skipped\n");
    console_print("  'write' cannot be used inside scripts\n");
    console_print("  Max script size: 4KB\n");
    console_print("  Filename must be 8.3  e.g. HELLO.SCP\n");
    console_print("\n");
    console_print("HOW TO CREATE ONE:\n");
    console_print("  On your PC, create a plain text file\n");
    console_print("  in server_files\\ then run addfiles.py\n");
    console_print("  to add it to the disk image, or use\n");
    console_print("  'get' to fetch it from the server.\n");
    console_print("\n");
    console_print("EXAMPLE (SETUP.SCP):\n");
    console_print("  # My startup script\n");
    console_print("  ver\n");
    console_print("  ls\n");
    console_print("  cat README.TXT\n");
    console_print("\n");
    console_print("RUN IT:  run SETUP.SCP\n");
    console_print("─────────────────────────────────────────\n");
}

static void cmd_ls(void) {
    console_print("Name            Size\n");
    console_print("--------------- ----\n");
    fat12_list_root(ls_callback);
}

static void cmd_cat(const char* filename) {
    filename = skip_spaces(filename);
    if (!*filename) { console_print("Usage: cat <filename>\n"); return; }
    FAT12_FileInfo info;
    int r = fat12_find(filename, &info);
    if (r == FAT12_ERR_NOTFOUND) {
        console_print("File not found: "); console_print(filename); console_newline(); return;
    }
    if (r != FAT12_OK)               { console_print("Disk error.\n"); return; }
    if (info.attributes & FAT_ATTR_DIRECTORY) { console_print("Cannot cat a directory.\n"); return; }
    if (info.file_size > sizeof(file_buf)) console_print("Warning: truncated to 32KB.\n");
    int bytes = fat12_read_file(&info, file_buf, sizeof(file_buf));
    if (bytes < 0) { console_print("Disk read error.\n"); return; }
    for (int i = 0; i < bytes; i++) {
        char c = (char)file_buf[i];
        if (c == '\r') continue;
        if (c == '\n') { console_newline(); continue; }
        if (c == '\t') { console_print("    "); continue; }
        if (c < 0x20 || c > 0x7E) continue;
        console_putchar(c);
    }
    console_newline();
}

static void cmd_netls(void) {
    console_print("Requesting file list from server...\n");
    int r = netfs_list(file_buf, sizeof(file_buf));
    if (r == NETFS_ERR_TIMEOUT) {
        console_print("Error: no response. Is esp32_server.py running?\n"); return;
    }
    if (r == NETFS_ERR_TOOBIG) { console_print("Error: response too large.\n"); return; }
    if (r == 0)                 { console_print("Server has no files.\n"); return; }
    console_print("Files on server:\n");
    int i = 0;
    while (i < r) {
        console_print("  ");
        while (i < r && file_buf[i] != '\n' && file_buf[i] != '\0')
            console_putchar((char)file_buf[i++]);
        console_newline();
        if (i < r && file_buf[i] == '\n') i++;
    }
}

static void cmd_get(const char* filename) {
    filename = skip_spaces(filename);
    if (!*filename) { console_print("Usage: get <filename>\n"); return; }
    console_print("Fetching: "); console_print(filename); console_print("...\n");
    int r = netfs_get(filename, file_buf, sizeof(file_buf));
    if (r == NETFS_ERR_TIMEOUT)  { console_print("Error: no response from server.\n"); return; }
    if (r == NETFS_ERR_NOTFOUND) { console_print("Error: file not found on server.\n"); return; }
    if (r == NETFS_ERR_TOOBIG)   { console_print("Error: file too large (max 32KB).\n"); return; }
    console_print("Received "); print_uint((uint32_t)r); console_print(" bytes:\n");
    for (int i = 0; i < r; i++) {
        char c = (char)file_buf[i];
        if (c == '\r') continue;
        if (c == '\n') { console_newline(); continue; }
        if (c == '\t') { console_print("    "); continue; }
        if (c < 0x20 || c > 0x7E) continue;
        console_putchar(c);
    }
    console_newline();
}

/* ── Write mode ──────────────────────────────────────────────────────────── */

static void write_mode_start(const char* filename) {
    filename = skip_spaces(filename);
    if (!*filename) { console_print("Usage: write <filename>\n"); return; }
    int i = 0;
    while (filename[i] && i < 31) { write_name[i] = filename[i]; i++; }
    write_name[i] = '\0';
    write_len  = 0;
    write_mode = 1;
    console_print("Writing to: "); console_print(write_name); console_newline();
    console_print("Type lines, then '.' alone on a line to save.\n");
    console_print("----------------------------------------\n");
}

static int write_mode_handle(const char* line) {
    if (line[0] == '.' && line[1] == '\0') {
        console_print("----------------------------------------\n");
        console_print("Sending to server...\n");
        int r = netfs_put(write_name, file_buf, write_len);
        if (r == NETFS_OK) {
            console_print("Saved: "); console_print(write_name);
            console_print(" ("); print_uint(write_len); console_print(" bytes)\n");
        } else if (r == NETFS_ERR_TIMEOUT) {
            console_print("Error: server not responding.\n");
        } else {
            console_print("Error: server rejected the file.\n");
        }
        write_mode = 0; write_len = 0;
        return 0;
    }
    int i = 0;
    while (line[i] && write_len < WRITE_BUF_SIZE - 2)
        file_buf[write_len++] = (uint8_t)line[i++];
    if (write_len < WRITE_BUF_SIZE - 2) {
        file_buf[write_len++] = '\r';
        file_buf[write_len++] = '\n';
    }
    return 1;
}

/* ── Script runner ───────────────────────────────────────────────────────── */

/* Forward declaration: cmd_run calls handle_command which is defined below */
static void handle_command(void);

static void cmd_run(const char* filename) {
    filename = skip_spaces(filename);
    if (!*filename) { console_print("Usage: run <filename>\n"); return; }
    FAT12_FileInfo info;
    if (fat12_find(filename, &info) != FAT12_OK) {
        console_print("Script not found: "); console_print(filename); console_newline(); return;
    }
    if (info.file_size > sizeof(script_buf)) {
        console_print("Script too large (max 4KB).\n"); return;
    }
    int len = fat12_read_file(&info, script_buf, sizeof(script_buf));
    if (len < 0) { console_print("Error reading script.\n"); return; }

    console_print("Running: "); console_print(filename); console_newline();
    char line[128]; int li = 0;
    for (int i = 0; i <= len; i++) {
        char c = (i < len) ? (char)script_buf[i] : '\n';
        if (c == '\r') continue;
        if (c != '\n') { if (li < 127) line[li++] = c; continue; }
        line[li] = '\0'; li = 0;
        const char* t = skip_spaces(line);
        if (!*t || *t == '#') continue;
        console_print("  > "); console_print(t); console_newline();
        input_len = 0;
        while (*t && input_len < 255) input_buf[input_len++] = *t++;
        handle_command();
    }
    console_print("Script done.\n");
}

/* ── Command dispatcher ──────────────────────────────────────────────────── */

static void handle_command(void) {
    input_buf[input_len] = '\0';
    console_newline();

    if (write_mode) {
        int still = write_mode_handle(input_buf);
        input_len = 0;
        if (still) { print_uint(write_len); console_putchar('>'); console_putchar(' '); }
        else        { print_prompt(); }
        return;
    }

    if      (input_len == 0)                      { /* empty */ }
    else if (str_eq(input_buf, "help scripts"))   cmd_help_scripts();
    else if (str_eq(input_buf, "help"))           cmd_help();
    else if (str_eq(input_buf, "clear"))          clear_screen();
    else if (str_eq(input_buf, "ver"))            console_print("MyOS v0.5 - 32-bit Protected Mode + FAT12\n");
    else if (str_eq(input_buf, "ls"))             cmd_ls();
    else if (str_starts(input_buf, "cat "))       cmd_cat(input_buf + 4);
    else if (str_starts(input_buf, "read "))      cmd_cat(input_buf + 5);
    else if (str_starts(input_buf, "write ")) {
        write_mode_start(input_buf + 6);
        print_uint(0); console_putchar('>'); console_putchar(' ');
        return;
    }
    else if (str_eq(input_buf, "write"))          console_print("Usage: write <filename>\n");
    else if (str_eq(input_buf, "netls"))          cmd_netls();
    else if (str_starts(input_buf, "get "))       cmd_get(input_buf + 4);
    else if (str_eq(input_buf, "get"))            console_print("Usage: get <filename>\n");
    else if (str_starts(input_buf, "run "))       cmd_run(input_buf + 4);
    else if (str_eq(input_buf, "run"))            console_print("Usage: run <filename>\n");
    else if (str_eq(input_buf, "time"))           cmd_time();
    else if (str_eq(input_buf, "cat") || str_eq(input_buf, "read"))
                                                  console_print("Usage: cat <filename>\n");
    else {
        console_print("Unknown command: "); console_print(input_buf); console_newline();
        console_print("Type 'help' for available commands.\n");
    }

    input_len = 0;
    print_prompt();
}

/* ── Input handler (called by keyboard driver) ───────────────────────────── */

void console_handle_input(char c) {
    if (c == '\n')  { handle_command(); return; }
    if (c == '\b')  {
        if (input_len > 0) {
            input_len--;
            if (--cursor_col < 0) cursor_col = 0;
            set_cell(cursor_row, cursor_col, ' ', current_color);
        }
        return;
    }
    if (input_len >= 255) return;
    input_buf[input_len++] = c;
    set_cell(cursor_row, cursor_col, c, current_color);
    advance_cursor();
}

/* ── Init ────────────────────────────────────────────────────────────────── */

void console_init(void) {
    clear_screen();
    console_print("MyOS v0.5 - 32-bit Protected Mode + FAT12\n");
    console_print("Type 'help' for available commands.\n");
    console_newline();
    print_prompt();
}