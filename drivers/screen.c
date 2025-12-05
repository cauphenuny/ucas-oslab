#include <screen.h>
#include <printk.h>
#include <os/string.h>
#include <os/sched.h>
#include <os/irq.h>
#include <os/kernel.h>

#define SCREEN_WIDTH    80
#define SCREEN_HEIGHT   50
#define SCREEN_LOC(x, y) ((y) * SCREEN_WIDTH + (x))

/* screen buffer */
char new_screen[SCREEN_HEIGHT * SCREEN_WIDTH] = {0};
char old_screen[SCREEN_HEIGHT * SCREEN_WIDTH] = {0};
int old_color[SCREEN_HEIGHT * SCREEN_WIDTH] = {0};
int scroll_start = -1, scroll_end = -1;
int color_trigger[SCREEN_HEIGHT * SCREEN_WIDTH] = {0};

/* cursor position */
static void vt100_move_cursor(int x, int y)
{
    // \033[y;xH
    printv("%c[%d;%dH", 27, y, x);
}

static void vt100_move_cursor_x(int x)
{
    // \033[xG
    printv("%c[%dG", 27, x);
}

static void vt100_move_cursor_y(int y)
{
    // \033[y;1H
    printv("%c[%dd", 27, y);
}

/* clear screen */
static void vt100_clear()
{
    // \033[2J
    printv("%c[2J", 27);
}

/* hidden cursor */
static void vt100_hidden_cursor()
{
    // \033[?25l
    printv("%c[?25l", 27);
}

void screen_scroll(int start_row, int end_row)
{
    int i, j;
    for (i = start_row; i < end_row; i++)
    {
        for (j = 0; j < SCREEN_WIDTH; j++)
        {
            new_screen[SCREEN_LOC(j, i)] = new_screen[SCREEN_LOC(j, i + 1)];
            color_trigger[SCREEN_LOC(j, i)] = color_trigger[SCREEN_LOC(j, i + 1)];
        }
    }
    for (j = 0; j < SCREEN_WIDTH; j++)
    {
        new_screen[SCREEN_LOC(j, end_row)] = ' ';
        color_trigger[SCREEN_LOC(j, end_row)] = 0;
    }
}

/* write a char */
/* write a char */
void screen_write_ch(char ch)
{
    if (ch == '\n')
    {
        current_running->cursor_x = 0;
        if (current_running->cursor_y == scroll_end) {
            screen_scroll(scroll_start, scroll_end);
        } else {
            if (current_running->cursor_y < SCREEN_HEIGHT) {
                current_running->cursor_y++;
            }
        }
    }
    else if (ch == '\b' || ch == '\177')
    {	
        // DONE: [P3] support backspace here
        int sum = current_running->cursor_y * SCREEN_WIDTH + current_running->cursor_x;
        if (sum == 0) {
            return;
        }
        if (current_running->cursor_x > 0)
            new_screen[SCREEN_LOC(current_running->cursor_x - 1, current_running->cursor_y)] = ' ';
        current_running->cursor_x--;
    }
    else
    {
        new_screen[SCREEN_LOC(current_running->cursor_x, current_running->cursor_y)] = ch;
        if (++current_running->cursor_x >= SCREEN_WIDTH)
        {
            current_running->cursor_x = 0;
            if (current_running->cursor_y < SCREEN_HEIGHT)
                current_running->cursor_y++;
        }
    }
}



void init_screen(void)
{
    vt100_hidden_cursor();
    vt100_clear();
    screen_clear();
}

void screen_clear(void)
{
    int i, j;
	vt100_clear();
    for (i = 0; i < SCREEN_HEIGHT; i++)
    {
        for (j = 0; j < SCREEN_WIDTH; j++)
        {
            new_screen[SCREEN_LOC(j, i)] = ' ';
			old_screen[SCREEN_LOC(j, i)] = ' ';
        }
    }
    current_running->cursor_x = 0;
    current_running->cursor_y = 0;
    memset(color_trigger, 0, sizeof(color_trigger));
    screen_reflush();
}

void screen_move_cursor(int x, int y)
{
    if (x >= SCREEN_WIDTH)
        x = SCREEN_WIDTH - 1;
    else if (x < 0)
        x = 0;
    if (y >= SCREEN_HEIGHT)
        y = SCREEN_HEIGHT - 1;
    else if (y < 0)
        y = 0;
    current_running->cursor_x = x;
    current_running->cursor_y = y;
    vt100_move_cursor(x + 1, y + 1);
}


void screen_move_cursor_row(int row)
{
    if (row >= SCREEN_HEIGHT)
        row = SCREEN_HEIGHT - 1;
    else if (row < 0)
        row = 0;
    current_running->cursor_y = row;
    vt100_move_cursor_y(row);
}

void screen_move_cursor_col(int col)
{
    if (col >= SCREEN_WIDTH)
        col = SCREEN_WIDTH - 1;
    else if (col < 0)
        col = 0;
    current_running->cursor_x = col;
    vt100_move_cursor_x(col);
}


void screen_write(char *buff)
{
    int i = 0;
    int l = strlen(buff);

    for (i = 0; i < l; i++)
    {
        screen_write_ch(buff[i]);
    }
}

/*
 * This function is used to print the serial port when the clock
 * interrupt is triggered. However, we need to pay attention to
 * the fact that in order to speed up printing, we only refresh
 * the characters that have been modified since this time.
 */
void screen_reflush(void)
{
    int i, j;

    int color = 0;
    static int last_color = 0;
    /* here to reflush screen buffer to serial port */
    for (i = 0; i < SCREEN_HEIGHT; i++)
    {
        for (j = 0; j < SCREEN_WIDTH; j++)
        {
            if (color_trigger[SCREEN_LOC(j, i)] != 0) {
                if (color_trigger[SCREEN_LOC(j, i)] > 0) {
                    color = color_trigger[SCREEN_LOC(j, i)];
                    // pretty_log(LOG_INFO, "trigger set color to %d at (%d, %d)", color, j, i);
                } else {
                    color = 0;
                    // pretty_log(LOG_INFO, "trigger reset color at (%d, %d)", j, i);
                }
            }
            /* We only print the data of the modified location. */
            if ((new_screen[SCREEN_LOC(j, i)] != old_screen[SCREEN_LOC(j, i)]) || (color != old_color[SCREEN_LOC(j, i)]))
            {
                vt100_move_cursor(j + 1, i + 1);
                if (color != old_color[SCREEN_LOC(j, i)] || last_color != color) {
                    printv("%c[%dm", 27, color);
                    // pretty_log(LOG_INFO, "set color to %d at (%d, %d)", color, j, i);
                }
                bios_putchar(new_screen[SCREEN_LOC(j, i)]);
                old_screen[SCREEN_LOC(j, i)] = new_screen[SCREEN_LOC(j, i)];
                old_color[SCREEN_LOC(j, i)] = color;
                last_color = color;
            }
        }
    }

    /* recover cursor position */
    vt100_move_cursor(current_running->cursor_x + 1, current_running->cursor_y + 1);
}

void screen_set_scroll(int start_row, int end_row)
{
    if (start_row < 0 || start_row >= SCREEN_HEIGHT ||
        end_row < 0 || end_row >= SCREEN_HEIGHT ||
        start_row >= end_row)
    {
        return;
    }
    scroll_start = start_row;
    scroll_end = end_row;
}

void screen_clear_scroll(void)
{
    scroll_start = -1;
    scroll_end = -1;
}


void screen_set_color(int start_col, int end_col, int foreground, int background)
{
    color_trigger[SCREEN_LOC(start_col, current_running->cursor_y)] = foreground;
    color_trigger[SCREEN_LOC(end_col, current_running->cursor_y)] = -foreground;
}

void screen_clear_color(void)
{
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        color_trigger[SCREEN_LOC(i, current_running->cursor_y)] = 0;
    }
}

void screen_delete_line(int nlines)
{
    if (nlines <= 0)
        return;
    int i, j;
    for (i = 0; i < nlines; i++)
    {
        for (j = 0; j < SCREEN_WIDTH; j++)
        {
            new_screen[SCREEN_LOC(j, current_running->cursor_y)] = ' ';
            color_trigger[SCREEN_LOC(j, current_running->cursor_y)] = 0;
        }
        current_running->cursor_y--;
    }
    current_running->cursor_x = 0, current_running->cursor_y++;
}

void screen_clear_lines(int start, int end) {
    for (int i = start; i < end; i++) {
        for (int j = 0; j < SCREEN_WIDTH; j++) {
            new_screen[SCREEN_LOC(j, i)] = ' ';
            color_trigger[SCREEN_LOC(j, i)] = 0;
        }
    }
}
