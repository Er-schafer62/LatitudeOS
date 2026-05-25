#ifndef CONSOLE_H
#define CONSOLE_H

void console_init();
void console_putchar(char c);
void console_print(const char* s);
void console_newline();
void console_handle_input(char c);

#endif