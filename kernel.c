#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "pit.h"
#include "pic.h"
#include "idt.h"
#include "keyboard_buffer.h"
#include "fs.h"
#include "ata.h"
#include "pci.h"
#include "ahci.h"
#include "mmio.h"
#include "ehci.h"

extern irq11();

typedef struct block {
    int size;
    int used;
    struct block* next;
} block_t;

#define HEAP_SIZE (10 * 1024 * 1024)
#define PAGE_SIZE 4096
#define MAX_PAGES (HEAP_SIZE / PAGE_SIZE)

static uint8_t phys_bitmap[MAX_PAGES / 8];
static char heap[HEAP_SIZE];
static block_t* free_list = NULL;

static void phys_set(int page) {
    phys_bitmap[page / 8] |= (1 << (page % 8));
}

static void phys_clear(int page) {
    phys_bitmap[page / 8] &= ~(1 << (page % 8));
}

static int phys_test(int page) {
    return phys_bitmap[page / 8] & (1 << (page % 8));
}

void* alloc_phys(int pages) {
    if (pages <= 0) return NULL;

    int start = -1;
    int count = 0;

    for (int i = 0; i < MAX_PAGES; i++) {

        if (!phys_test(i)) {

            if (start == -1)
                start = i;

            count++;

            if (count == pages) {

                for (int j = 0; j < pages; j++) {
                    phys_set(start + j);
                }

                return (void*)(uintptr_t)(start * PAGE_SIZE);
            }

        }
        else {
            start = -1;
            count = 0;
        }
    }

    return NULL;
}

void free_phys(void* addr, int pages) {
    if (!addr || pages <= 0) return;

    int start = (uintptr_t)addr / PAGE_SIZE;

    for (int i = 0; i < pages; i++) {
        phys_clear(start + i);
    }
}

void init_phys() {
    for (int i = 0; i < sizeof(phys_bitmap); i++) {
        phys_bitmap[i] = 0;
    }

    int kernel_pages = 1024;

    for (int i = 0; i < kernel_pages; i++) {
        phys_set(i);
    }
}

static void init_heap(void) {
    free_list = (block_t*)heap;
    free_list->size = 65536 - sizeof(block_t);
    free_list->used = 0;
    free_list->next = NULL;
}

void* malloc(int size) {
    if (!free_list) init_heap();
    if (size <= 0) return NULL;

    block_t* prev = NULL;
    block_t* curr = free_list;

    while (curr) {
        if (!curr->used && curr->size >= size) {

            if (curr->size > size + sizeof(block_t)) {
                block_t* new_block = (block_t*)((char*)curr + sizeof(block_t) + size);
                new_block->size = curr->size - size - sizeof(block_t);
                new_block->used = 0;
                new_block->next = curr->next;

                curr->size = size;
                curr->next = new_block;
            }
            curr->used = 1;
            return (void*)((char*)curr + sizeof(block_t));
        }
        prev = curr;
        curr = curr->next;
    }
    return NULL;
}

void free(void* ptr) {
    if (!ptr) return;

    block_t* block = (block_t*)((char*)ptr - sizeof(block_t));
    block->used = 0;

    block_t* curr = free_list;
    while (curr && curr->next) {
        if (!curr->used && !curr->next->used &&
            (char*)curr + sizeof(block_t) + curr->size == (char*)curr->next) {
            curr->size += sizeof(block_t) + curr->next->size;
            curr->next = curr->next->next;
        }
        else {
            curr = curr->next;
        }
    }
}

double parse_expr(const char* s, int* i);
double parse_term(const char* s, int* i);
double parse_factor(const char* s, int* i);

void hide_cursor();
void show_cursor();

int create_mbr_partition(block_device_t* dev, int part_num, uint32_t start_sector, uint32_t size_mb);

int assemble_and_execute(const char* asm_line);

#define VIDEO_MEMORY 0xB8000

int cursor = 0;
int shift = 0;
int cursor_visible = 1;

char cursor_saved_char = ' ';
char cursor_saved_attr = 0x07;
int cursor_pos = 0;

int caps = 0;

#define MAX_VARS 16

typedef enum {
    TYPE_INT,
    TYPE_DOUBLE,
    TYPE_STRING
} VarType;

typedef struct {
    char name[16];
    VarType type;

    int i_val;
    double d_val;
    char s_val[32];
} Variable;

Variable vars[MAX_VARS];
int var_count = 0;

int current_bg = 0x00;
int current_fg = 0x07;
int current_cursor = 0x0F;

const char* msg_title = "=== Bare Metal Calculator ===\n";
const char* msg_commands = "Commands: help, clear, cls, exit\n";
const char* msg_examples = "Examples: 10+5, 100-33, 6*7, 100/4, 17%%3, 2^3, sqrt(16)\n\n";
const char* msg_result = "Result: ";
const char* msg_error = "Error: Invalid expression!\n";
const char* msg_error_format = "Use: number +-*/%% number or function\n";
const char* msg_reboot = "Rebooting...\n";

const char* msg_help =
"Financial:\n"
"  vat <sum> [rate]  - VAT (default 20%%)\n"
"  pct <part> <total> - Percentage\n"
"  rch <num> <digits> - Round to N digits\n"
"  ceil <num> / floor <num> / abs <num>\n"
"Stats:\n"
"  sum / avg / min / max <n1> <n2> ...\n"
"Math:\n"
"  sqrt <num> / <expr> + - * / %% ^ and ()\n"
"Variables:\n"
"  var <name> = <value> / echo / type / list_vars / delete\n"
"Interface:\n"
"  bg-color / char-color / cursor-color <color>\n"
"  clear / cls\n"
"System:\n"
"  time / help / exit\n";

char scancode_table[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',
    0, '*',
    0, ' ',
};

const char* exception_names[32] = {
    "Divide Error (#DE)",
    "Debug (#DB)",
    "NMI",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "BOUND Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",
    "Device Not Available (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack Fault (#SS)",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Reserved",
    "FPU Fault (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Fault (#XF)",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

void print_prompt() {
    char path[MAX_PATH];
    get_pwd_string(path, MAX_PATH);
    print(path);
    print("> ");
}

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t intr, err;
    uint32_t eip, cs, eflags, useresp, ss;
} regs_t;

int strcmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

int strncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

void* memset(void* dest, int value, size_t count) {
    uint8_t* ptr = (uint8_t*)dest;

    for (size_t i = 0; i < count; i++) {
        ptr[i] = (uint8_t)value;
    }

    return dest;
}

int strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while (*src) *d++ = *src++;
    *d = 0;
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;

    while (*d) d++;

    while (*src) *d++ = *src++;
    *d = 0;
    return dest;
}

void exception_handler(regs_t* r) {
    clear();

    print("========== EXCEPTION ==========\n");

    if (r->intr < 32) {
        print(exception_names[r->intr]);
    }
    else {
        print("Unknown Exception: ");
        print_hex(r->intr);
    }
    print("\n\n");

    print("Error code: ");
    print_hex(r->err);
    print("\n\n");

    print("Registers:\n");
    print("EAX="); print_hex(r->eax); print(" EBX="); print_hex(r->ebx);
    print(" ECX="); print_hex(r->ecx); print(" EDX="); print_hex(r->edx);
    print("\n");
    print("ESI="); print_hex(r->esi); print(" EDI="); print_hex(r->edi);
    print(" EBP="); print_hex(r->ebp); print(" ESP="); print_hex(r->esp);
    print("\n");
    print("EIP="); print_hex(r->eip); print(" CS="); print_hex(r->cs);
    print(" EFLAGS="); print_hex(r->eflags);
    print("\n\n");

    print("Waiting 5 seconds, then press any key to reboot...\n");

    for (volatile int i = 0; i < 5000000; i++) {
        __asm__ volatile ("pause");
    }

    while (inb(0x64) & 1) {
        inb(0x60);
    }

    print("Press any key to reboot...\n");

    while (1) {
        if (inb(0x64) & 1) {
            inb(0x60);
            break;
        }
        __asm__ volatile ("pause");
    }

    reboot();
}

void reboot() {
    __asm__ volatile ("cli");
    outb(0x64, 0xFE);
    while (1) __asm__ volatile ("hlt");
}

void print_char(char c) {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    hide_cursor();
    if (c == '\n') {
        int line = cursor / (80 * 2);
        cursor = (line + 1) * 80 * 2;
    }
    else {
        video[cursor] = c;
        video[cursor + 1] = current_fg | (current_bg << 4);
        cursor += 2;
    }
    show_cursor();
}

void print(const char* str) {
    while (*str) print_char(*str++);
}

void print_hex(uint32_t num) {
    print("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t digit = (num >> i) & 0xF;
        if (digit < 10) print_char('0' + digit);
        else print_char('A' + (digit - 10));
    }
}

void clear() {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = current_fg | (current_bg << 4);
    }
    cursor = 0;
}

double parse_double(const char* s) {
    double res = 0.0, frac = 0.1;
    int i = 0;

    while (s[i] && s[i] >= '0' && s[i] <= '9') {
        res = res * 10.0 + (s[i] - '0');
        i++;
    }

    if (s[i] == '.') {
        i++;
        while (s[i] && s[i] >= '0' && s[i] <= '9') {
            res += (s[i] - '0') * frac;
            frac *= 0.1;
            i++;
        }
    }

    return res;
}

void print_double(double num) {
    if (num < 0) {
        print_char('-');
        num = -num;
    }

    int int_part = (int)num;
    double frac_part = num - int_part;

    if (int_part == 0) {
        print_char('0');
    }
    else {
        char buf[32];
        int i = 0;
        while (int_part > 0) {
            buf[i++] = '0' + (int_part % 10);
            int_part /= 10;
        }
        while (i > 0) print_char(buf[--i]);
    }

    print_char('.');
    int frac_int = (int)(frac_part * 100);
    if (frac_int < 0) frac_int = -frac_int;
    char fbuf[3];
    fbuf[0] = '0' + (frac_int / 10) % 10;
    fbuf[1] = '0' + (frac_int % 10);
    fbuf[2] = '\0';
    print(fbuf);
}

void hide_cursor() {
    volatile char* video = (volatile char*)VIDEO_MEMORY;

    if (cursor_pos >= 0) {
        video[cursor_pos] = cursor_saved_char;
        video[cursor_pos + 1] = cursor_saved_attr;
    }
}

void show_cursor() {
    volatile char* video = (volatile char*)VIDEO_MEMORY;
    cursor_pos = cursor;

    if (video[cursor] == '_') return;

    cursor_saved_char = video[cursor];
    cursor_saved_attr = video[cursor + 1];

    video[cursor] = '_';
    video[cursor + 1] = current_cursor | (current_bg << 4);
}

Variable* find_var(const char* name) {
    for (int i = 0; i < var_count; i++) {
        int j = 0;
        while (vars[i].name[j] && name[j] && vars[i].name[j] == name[j]) j++;

        if (vars[i].name[j] == 0 && name[j] == 0)
            return &vars[i];
    }
    return 0;
}

void set_var(const char* name, const char* value) {
    Variable* v = find_var(name);

    if (!v && var_count < MAX_VARS) {
        v = &vars[var_count++];
    }

    int is_string = (value[0] == '"');

    int is_double = 0;
    int has_dot = 0;

    for (int i = 0; value[i]; i++)
        if (value[i] == '.') has_dot = 1;

    if (is_string) {
        v->type = TYPE_STRING;

        int i = 1;
        int j = 0;

        while (value[i] && value[i] != '"' && j < 31)
            v->s_val[j++] = value[i++];

        v->s_val[j] = 0;
    }
    else if (has_dot) {
        v->type = TYPE_DOUBLE;
        v->d_val = parse_double(value);
    }
    else {
        v->type = TYPE_INT;
        v->i_val = (int)parse_double(value);
    }

    int k = 0;
    while (name[k] && k < 15) {
        v->name[k] = name[k];
        k++;
    }
    v->name[k] = 0;
}

void cmd_echo(const char* name) {
    Variable* v = find_var(name);
    if (!v) {
        print("undefined\n");
        return;
    }

    if (v->type == TYPE_INT) {
        print_double(v->i_val);
    }
    else if (v->type == TYPE_DOUBLE) {
        print_double(v->d_val);
    }
    else {
        print(v->s_val);
    }

    print("\n");
}

void cmd_type(const char* name) {
    Variable* v = find_var(name);

    if (!v) {
        print("undefined\n");
        return;
    }

    if (v->type == TYPE_INT) print("int\n");
    else if (v->type == TYPE_DOUBLE) print("double\n");
    else print("string\n");
}

void cmd_print(const char* text) {
    int i = 0;

    if (text[0] == '"') i = 1;

    while (text[i] && text[i] != '"') {
        print_char(text[i]);
        i++;
    }

    print("\n");
}

void delete_var(const char* name) {
    for (int i = 0; i < var_count; i++) {
        int j = 0;

        while (vars[i].name[j] == name[j] && name[j]) j++;

        if (vars[i].name[j] == 0 && name[j] == 0) {

            for (int k = i; k < var_count - 1; k++) {
                vars[k] = vars[k + 1];
            }

            var_count--;
            return;
        }
    }
}

void list_vars() {
    for (int i = 0; i < var_count; i++) {

        print(vars[i].name);
        print(" = ");

        if (vars[i].type == TYPE_INT) {
            print_double(vars[i].i_val);
        }
        else if (vars[i].type == TYPE_DOUBLE) {
            print_double(vars[i].d_val);
        }
        else {
            print(vars[i].s_val);
        }

        print("\n");
    }
}

void read_line(char* buffer, int max_len) {
    int len = 0;
    int pos = 0;

    int line_start = cursor;

    flush_keys();

    while (1) {

        if (!has_key()) {
            __asm__ volatile ("hlt");
            continue;
        }

        unsigned char scancode = get_key();

        if (scancode == 0xE0) {

            while (!has_key()) {
                __asm__ volatile ("hlt");
            }
            unsigned char sc2 = get_key();

            if (sc2 & 0x80) continue;

            if (sc2 == 0x4B && pos > 0) {
                hide_cursor();
                pos--;
                cursor -= 2;
                show_cursor();
            }

            if (sc2 == 0x4D && pos < len) {
                hide_cursor();
                pos++;
                cursor += 2;
                show_cursor();
            }

            continue;
        }

        if (scancode == 0x2A || scancode == 0x36) { shift = 1; continue; }
        if (scancode == 0xAA || scancode == 0xB6) { shift = 0; continue; }

        if (scancode == 0x3A) { caps ^= 1; continue; }

        if (scancode & 0x80) continue;

        char c = scancode_table[scancode];
        if (!c) continue;

        if (shift) {
            if ((shift ^ caps) && c >= 'a' && c <= 'z') {
                c -= 32;
            }
            else {
                switch (c) {
                case '1': c = '!'; break;
                case '2': c = '@'; break;
                case '3': c = '#'; break;
                case '4': c = '$'; break;
                case '5': c = '%'; break;
                case '6': c = '^'; break;
                case '7': c = '&'; break;
                case '8': c = '*'; break;
                case '9': c = '('; break;
                case '0': c = ')'; break;
                case '-': c = '_'; break;
                case '=': c = '+'; break;
                case '[': c = '{'; break;
                case ']': c = '}'; break;
                case ';': c = ':'; break;
                case '\'': c = '"'; break;
                case ',': c = '<'; break;
                case '.': c = '>'; break;
                case '/': c = '?'; break;
                case '\\': c = '|'; break;
                case '`': c = '~'; break;
                }
            }
        }

        if (c == '\n') {
            buffer[len] = '\0';
            print_char('\n');
            return;
        }

        if (c == '\b') {
            if (pos > 0) {
                hide_cursor();

                for (int j = pos - 1; j < len - 1; j++)
                    buffer[j] = buffer[j + 1];

                len--;
                pos--;

                cursor = line_start;

                for (int i = 0; i < len; i++) {
                    volatile char* video = (volatile char*)VIDEO_MEMORY;
                    video[cursor] = buffer[i];
                    video[cursor + 1] = current_fg | (current_bg << 4);
                    cursor += 2;
                }

                volatile char* video = (volatile char*)VIDEO_MEMORY;
                video[cursor] = ' ';
                video[cursor + 1] = current_fg | (current_bg << 4);

                cursor = line_start + pos * 2;
                show_cursor();
            }
            continue;
        }

        if (len < max_len - 1) {
            hide_cursor();

            for (int j = len; j > pos; j--)
                buffer[j] = buffer[j - 1];

            buffer[pos] = c;
            len++;
            pos++;

            cursor = line_start;

            for (int i = 0; i < len; i++) {
                volatile char* video = (volatile char*)VIDEO_MEMORY;
                video[cursor] = buffer[i];
                video[cursor + 1] = current_fg | (current_bg << 4);
                cursor += 2;
            }

            volatile char* video = (volatile char*)VIDEO_MEMORY;
            video[cursor] = ' ';
            video[cursor + 1] = current_fg | (current_bg << 4);

            cursor = line_start + pos * 2;
            show_cursor();
            continue;
        }
    }
}

double get_var_double(const char* name) {
    Variable* v = find_var(name);
    if (!v) return 0;

    if (v->type == TYPE_INT) return v->i_val;
    if (v->type == TYPE_DOUBLE) return v->d_val;
    return 0;
}

int is_number(char c) {
    return (c >= '0' && c <= '9') || c == '-' || c == '.';
}

double sqrt_fast(double x) {
    if (x < 0) return -1;
    if (x == 0) return 0;
    if (x == 1) return 1;

    double low = 0;
    double high = x;
    if (x < 1) high = 1;

    for (int i = 0; i < 100; i++) {
        double mid = (low + high) / 2;
        double sq = mid * mid;
        if (sq < x) low = mid;
        else high = mid;
    }
    return (low + high) / 2;
}

double parse_factor(const char* s, int* i) {
    while (s[*i] == ' ') (*i)++;

    if (s[*i] == '-') {
        (*i)++;
        return -parse_factor(s, i);
    }

    if (s[*i] == '(') {
        (*i)++;
        double val = parse_expr(s, i);
        if (s[*i] == ')') (*i)++;
        return val;
    }

    if (s[*i] >= '0' && s[*i] <= '9') {
        double val = parse_double(&s[*i]);
        while ((s[*i] >= '0' && s[*i] <= '9') || s[*i] == '.') (*i)++;
        return val;
    }

    if ((s[*i] >= 'a' && s[*i] <= 'z') || (s[*i] >= 'A' && s[*i] <= 'Z')) {
        char name[16];
        int n = 0;
        while ((s[*i] >= 'a' && s[*i] <= 'z') || (s[*i] >= 'A' && s[*i] <= 'Z')) {
            name[n++] = s[*i];
            (*i)++;
        }
        name[n] = 0;
        while (s[*i] == ' ') (*i)++;

        return get_var_double(name);
    }

    return 0;
}

double pow(double a, double b) {

    if (b == 0) return 1;

    if (b == (int)b) {
        int n = (int)b;

        double res = 1.0;
        int neg = 0;

        if (n < 0) {
            neg = 1;
            n = -n;
        }

        for (int i = 0; i < n; i++)
            res *= a;

        return neg ? 1.0 / res : res;
    }

    if (b == 0.5) {
        return sqrt_fast(a);
    }

    if (b > 0 && b < 1) {

        double r = sqrt_fast(a);

        return r * (b * 2.0 + (1.0 - b));
    }

    return 0;
}

double parse_power(const char* s, int* i) {
    double val = parse_factor(s, i);

    while (1) {
        while (s[*i] == ' ') (*i)++;

        if (s[*i] != '^')
            break;

        (*i)++;

        double rhs = parse_factor(s, i);

        val = pow(val, rhs);
    }

    return val;
}

double parse_term(const char* s, int* i) {
    double val = parse_power(s, i);

    while (1) {
        while (s[*i] == ' ') (*i)++;

        if (s[*i] == '/' && s[*i + 1] == '/') {
            (*i) += 2;
            double rhs = parse_power(s, i);

            val = (int)(val / rhs);
            continue;
        }

        char op = s[*i];

        if (op != '*' && op != '/' && op != '%')
            break;

        (*i)++;

        double rhs = parse_power(s, i);

        if (op == '*') val *= rhs;
        else if (op == '/') val /= rhs;
        else val = (int)val % (int)rhs;
    }

    return val;
}

double parse_expr(const char* s, int* i) {
    double left = parse_term(s, i);

    while (1) {
        while (s[*i] == ' ') (*i)++;

        char op = s[*i];
        if (op != '+' && op != '-') break;

        (*i)++;

        double right = parse_term(s, i);

        if (op == '+') left += right;
        else left -= right;
    }

    return left;
}

int parse_and_calc_double(const char* input, double* result) {
    int i = 0;

    *result = parse_expr(input, &i);

    while (input[i] == ' ') i++;

    if (input[i] != '\0')
        return 0;

    return 1;
}

int is_round_command(const char* input, double* result) {
    int i = 0;
    while (input[i] == ' ') i++;
    if (input[i] != 'r' && input[i] != 'R') return 0;
    i++;
    while (input[i] == ' ') i++;
    if (input[i] == '\0') return 0;
    double num = parse_double(&input[i]);
    if (num >= 0) *result = (int)(num + 0.5);
    else *result = (int)(num - 0.5);
    return 1;
}

int starts_with(const char* s, const char* p) {
    while (*p) {
        if (*s++ != *p++) return 0;
    }
    return 1;
}

int streq(const char* a, const char* b) {
    return strcmp(a, b);
}

int bcd_to_bin(int val) {
    return (val & 0x0F) + ((val >> 4) * 10);
}

unsigned char cmos_read(unsigned char reg) {
    outb(0x70, reg);
    return inb(0x71);
}

void cmd_time() {
    int sec = bcd_to_bin(cmos_read(0x00));
    int min = bcd_to_bin(cmos_read(0x02));
    int hour = bcd_to_bin(cmos_read(0x04));

    hour = hour % 24;

    if (hour < 10) print_char('0');
    print_double(hour);

    print_char(':');

    if (min < 10) print_char('0');
    print_double(min);

    print_char(':');

    if (sec < 10) print_char('0');
    print_double(sec);

    print("\n");
}

void cursor_blink() {
    if (cursor_visible) {
        hide_cursor();
        cursor_visible = 0;
    }
    else {
        show_cursor();
        cursor_visible = 1;
    }
}

void disable_hardware_cursor() {

    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x00);
}

void cmd_mount(const char* cmd) {
    const char* p = cmd + 6;

    char dev[32];
    char path[64];
    int i = 0;

    while (*p && *p != ' ' && i < 31) dev[i++] = *p++;
    dev[i] = 0;
    while (*p == ' ') p++;

    i = 0;
    while (*p && i < 63) path[i++] = *p++;
    path[i] = 0;

    if (!dev[0] || !path[0]) {
        print("Usage: mount /dev/hda1 /mnt\n");
        return;
    }

    char abs_path[64];
    if (path[0] != '/') {
        abs_path[0] = '/';
        int j = 0;
        while (path[j] && j < 62) {
            abs_path[j + 1] = path[j];
            j++;
        }
        abs_path[j + 1] = 0;
    }
    else {
        strcpy(abs_path, path);
    }

    int len = strlen(dev);
    int has_number = (dev[len - 1] >= '0' && dev[len - 1] <= '9');

    if (!has_number) {
        print("Error: Can't mount whole disk, use partition like /dev/hda1\n");
        return;
    }

    block_device_t* b = block_find(dev + 5);
    if (!b || !b->present) {
        print("Device not found\n");
        return;
    }

    uint8_t magic[512];
    b->read(b, SUPERBLOCK_SECTOR, 1, magic);
    if (magic[0] != 'M' || magic[1] != 'F' || magic[2] != 'S') {
        print("No filesystem, formatting...\n");
        fs_format(b);
    }

    if (fs_mount(b, abs_path)) {
        print("Mounted ");
        print(dev);
        print(" to ");
        print(abs_path);
        print("\n");
    }
    else {
        print("Mount failed\n");
    }
}

void kernel_main() {
    __asm__ volatile ("finit");

    init_phys();

    pic_remap();
    idt_init();
    init_pit();

    pic_enable_irq(0);
    pic_enable_irq(1);

    clear();
    disable_hardware_cursor();

    pci_init();
    block_init();
    fs_init();

    __asm__ volatile ("sti");

    show_cursor();

    print(msg_title);
    print(msg_commands);
    print(msg_examples);

    char input[64];
    double res;

    uint32_t last_poll = 0;

    while (1) {
        print_prompt();

        uint32_t ticks = get_ticks();
        if (ticks - last_poll > 2) {
            ehci_poll_ports();
            last_poll = ticks;
        }

        read_line(input, sizeof(input));

        if (starts_with(input, "bigwrite ")) {
            char* p = input + 9;
            while (*p == ' ') p++;
            char* path = p;
            while (*p && *p != ' ') p++;
            if (*p) {
                *p = 0;
                p++;
                while (*p == ' ') p++;
                int size = 0;
                while (*p >= '0' && *p <= '9') {
                    size = size * 10 + (*p - '0');
                    p++;
                }

                char* buf = malloc(size + 1);
                for (int i = 0; i < size; i++) buf[i] = 'A' + (i % 26);
                buf[size] = 0;

                fs_write_file(path, buf);
                free(buf);
                print("Written\n");
            }
            continue;
        }

        if (starts_with(input, "size ")) {
            int sz = fs_get_file_size(input + 5);
            if (sz < 0) print("Not found\n");
            else {
                print("Size: ");
                print_hex(sz);
                print(" bytes\n");
            }
            continue;
        }

        if (starts_with(input, "append ")) {
            char* p = input + 7;
            while (*p == ' ') p++;
            char* path = p;
            while (*p && *p != ' ') p++;
            if (*p) {
                *p = 0;
                p++;
                while (*p == ' ') p++;

                int size = fs_get_file_size(path);
                if (size < 0) {
                    print("File not found\n");
                    continue;
                }

                char* old = malloc(size + 1);
                fs_read_file_to_buf(path, old, size);
                old[size] = 0;

                int new_len = size + strlen(p);
                char* buf = malloc(new_len + 1);
                strcpy(buf, old);
                strcat(buf, p);

                fs_write_file(path, buf);

                free(old);
                free(buf);
                print("Appended\n");
            }
            continue;
        }

        if (starts_with(input, "cp ")) {
            char* p = input + 3;
            while (*p == ' ') p++;
            char* src = p;
            while (*p && *p != ' ') p++;
            if (*p) {
                *p = 0;
                p++;
                while (*p == ' ') p++;

                int size = fs_get_file_size(src);
                if (size < 0) {
                    print("Source not found\n");
                    continue;
                }

                char* buf = (char*)malloc(size + 1);
                if (!buf) {
                    print("Out of memory\n");
                    continue;
                }

                int read_size = fs_read_file_to_buf(src, buf, size);
                if (read_size < 0) {
                    print("Read failed\n");
                    free(buf);
                    continue;
                }
                buf[read_size] = 0;

                if (!fs_create_file(p)) {
                    print("Cannot create destination\n");
                    free(buf);
                    continue;
                }

                if (fs_write_file(p, buf)) {
                    print("Copied\n");
                }
                else {
                    print("Write failed\n");
                }
                free(buf);
            }
            else {
                print("Usage: cp <source> <dest>\n");
            }
            continue;
        }

        if (starts_with(input, "mv ")) {
            char* p = input + 3;
            while (*p == ' ') p++;
            char* src = p;
            while (*p && *p != ' ') p++;
            if (*p) {
                *p = 0;
                p++;
                while (*p == ' ') p++;

                int size = fs_get_file_size(src);
                if (size < 0) {
                    print("Source not found\n");
                    continue;
                }

                char* buf = (char*)malloc(size + 1);
                if (!buf) {
                    print("Out of memory\n");
                    continue;
                }

                int read_size = fs_read_file_to_buf(src, buf, size);
                if (read_size < 0) {
                    print("Read failed\n");
                    free(buf);
                    continue;
                }
                buf[read_size] = 0;

                if (!fs_create_file(p)) {
                    print("Cannot create destination\n");
                    free(buf);
                    continue;
                }

                if (fs_write_file(p, buf)) {
                    fs_remove_file(src);
                    print("Moved\n");
                }
                else {
                    print("Write failed\n");
                }
                free(buf);
            }
            else {
                print("Usage: mv <source> <dest>\n");
            }
            continue;
        }

        if (strncmp(input, "mount ", 6) == 0) {
            cmd_mount(input);
            continue;
        }

        if (starts_with(input, "symlink ")) {
            char* p = input + 8;
            while (*p == ' ') p++;
            char* target = p;
            while (*p && *p != ' ') p++;
            if (*p) {
                *p = 0;
                p++;
                while (*p == ' ') p++;
                if (vfs_create_symlink(target, p)) {
                    print("Symlink created\n");
                }
                else {
                    print("Failed\n");
                }
            }
            else {
                print("Usage: symlink <target> <linkname>\n");
            }
            continue;
        }

        if (starts_with(input, "readlink ")) {
            vfs_readlink(input + 9);
            continue;
        }

        if (streq(input, "showpart") || streq(input, "showpartitions")) {
            print("Partitions:\n");
            mbr_t mbr;
            block_device_t* dev = block_find("hda");
            if (dev && dev->read(dev, 0, 1, &mbr) && mbr.signature == MBR_SIGNATURE) {
                for (int i = 0; i < 4; i++) {
                    if (mbr.parts[i].type != 0) {
                        print("/dev/hda");
                        print_hex(i + 1);
                        print(": type=0x");
                        print_hex(mbr.parts[i].type);
                        print(", start=");
                        print_hex(mbr.parts[i].lba_start);
                        print(", size=");
                        print_hex(mbr.parts[i].sector_count / 2048);
                        print(" MB\n");
                    }
                    else {
                        print("/dev/hda");
                        print_hex(i + 1);
                        print(": empty\n");
                    }
                }
            }
            continue;
        }

        if (starts_with(input, "delpart ")) {
            char devname[16];
            int part_num = 0;

            int i = 8;
            while (input[i] == ' ') i++;

            int j = 0;
            while (input[i] && input[i] != ' ' && j < 15) {
                devname[j++] = input[i++];
            }
            devname[j] = 0;

            char full_devname[16];
            strcpy(full_devname, devname + 5);

            int len = strlen(devname);
            if (len > 0 && devname[len - 1] >= '0' && devname[len - 1] <= '9') {
                part_num = devname[len - 1] - '0';
                devname[len - 1] = 0;
            }
            else {
                print("Invalid partition name\n");
                continue;
            }

            if (part_num < 1 || part_num > 4) {
                print("Partition number must be 1-4\n");
                continue;
            }

            block_device_t* dev = block_find(devname + 5);
            if (!dev || !dev->present) {
                print("Device not found\n");
                continue;
            }

            if (delete_mbr_partition(dev, part_num)) {
                print("Partition deleted\n");

                fs_remove_device(full_devname);

                fs_update_devices();
            }
            else {
                print("Delete failed\n");
            }
            continue;
        }

        if (starts_with(input, "mkpart ")) {
            char devname[16];
            int part_num = 0;
            int size_mb = 0;

            int i = 7;
            while (input[i] == ' ') i++;

            int j = 0;
            while (input[i] && input[i] != ' ' && j < 15) {
                devname[j++] = input[i++];
            }
            devname[j] = 0;
            while (input[i] == ' ') i++;

            while (input[i] >= '0' && input[i] <= '9') {
                part_num = part_num * 10 + (input[i] - '0');
                i++;
            }
            while (input[i] == ' ') i++;

            while (input[i] >= '0' && input[i] <= '9') {
                size_mb = size_mb * 10 + (input[i] - '0');
                i++;
            }

            if (part_num < 1 || part_num > 4) {
                print("Partition number must be 1-4\n");
                continue;
            }

            if (size_mb < 1) {
                print("Size must be positive\n");
                continue;
            }

            block_device_t* dev = block_find(devname + 5);
            if (!dev || !dev->present) {
                print("Device not found\n");
                continue;
            }

            mbr_t mbr;
            uint32_t start_sector = 2048;

            if (dev->read(dev, 0, 1, &mbr) && mbr.signature == MBR_SIGNATURE) {
                uint32_t max_end = 2048;
                for (int k = 0; k < 4; k++) {
                    if (mbr.parts[k].type != 0) {
                        uint32_t end = mbr.parts[k].lba_start + mbr.parts[k].sector_count;
                        if (end > max_end) max_end = end;
                    }
                }
                start_sector = max_end;
            }

            if (create_mbr_partition(dev, part_num, start_sector, size_mb)) {
                print("Partition ");
                print_hex(part_num);
                print(" created: ");
                print_hex(size_mb);
                print(" MB\n");

                update_partitions_and_dev();

                fs_update_devices();
            }
            else {
                print("Failed to create partition\n");
            }
            continue;
        }

        if (starts_with(input, "umount ")) {
            char mntpoint[MAX_PATH];
            int i = 7;
            while (input[i] == ' ') i++;
            int j = 0;
            while (input[i] && input[i] != ' ' && j < MAX_PATH - 1) {
                mntpoint[j++] = input[i++];
            }
            mntpoint[j] = 0;
            fs_umount(mntpoint);
            continue;
        }

        if (starts_with(input, "rm ")) {
            if (!vfs_remove_file(input + 3))
                print("Failed\n");
            continue;
        }

        if (strncmp(input, "format ", 7) == 0) {
            const char* path = input + 7;

            if (fs_format_device(path)) {
                print("Formatted ");
                print(path);
                print("\n");
            }
            else {
                print("Format failed\n");
            }
            continue;
        }

        if (starts_with(input, "rmdir ")) {
            if (!vfs_remove_dir(input + 6))
                print("Failed\n");
            continue;
        }

        if (starts_with(input, "new ")) {
            if (!vfs_create_file(input + 4))
                print("Failed\n");
            continue;
        }

        if (starts_with(input, "newdir ")) {
            if (!vfs_create_dir(input + 7))
                print("Failed\n");
            continue;
        }

        if (starts_with(input, "write ")) {
            char* space = input + 6;
            while (*space == ' ') space++;
            char* path = space;
            while (*space && *space != ' ') space++;
            if (*space) {
                *space = 0;
                space++;
                while (*space == ' ') space++;
                vfs_write(path, space);
            }
            continue;
        }

        if (starts_with(input, "cat ")) {
            vfs_read(input + 4);
            continue;
        }

        if (starts_with(input, "ls ")) {
            vfs_list(input + 3);
            continue;
        }

        if (starts_with(input, "dir ")) {
            vfs_list(input + 4);
            continue;
        }

        if (starts_with(input, "cd ")) {
            vfs_cd(input + 3);
            continue;
        }

        if (streq(input, "pwd")) {
            fs_pwd();
            continue;
        }

        if (streq(input, "ls") || streq(input, "dir")) {
            vfs_list("");
            continue;
        }

        if (starts_with(input, "rch ")) {
            const char* p = input + 4;
            while (*p == ' ') p++;
            double x = parse_double(p);
            while (*p && (*p >= '0' || *p == '.' || *p == '-')) p++;
            while (*p == ' ') p++;
            double n = parse_double(p);

            double mult = 1.0;
            for (int i = 0; i < (int)n; i++) mult *= 10.0;
            double res = (double)(int)(x * mult + (x > 0 ? 0.5 : -0.5)) / mult;
            print("Result: ");
            print_double(res);
            print("\n");
            continue;
        }

        if (starts_with(input, "asm ")) {
            const char* asm_code = input + 4;
            if (!assemble_and_execute(asm_code)) {
                print("Failed to assemble/execute\n");
            }
            continue;
        }

        if (starts_with(input, "ceil ")) {
            const char* p = input + 5;
            while (*p == ' ') p++;
            double x = parse_double(p);
            double res = (double)(int)(x + 0.999999);
            print("Result: ");
            print_double(res);
            print("\n");
            continue;
        }

        if (starts_with(input, "floor ")) {
            const char* p = input + 6;
            while (*p == ' ') p++;
            double x = parse_double(p);
            double res = (double)(int)x;
            print("Result: ");
            print_double(res);
            print("\n");
            continue;
        }

        if (starts_with(input, "abs ")) {
            const char* p = input + 4;
            while (*p == ' ') p++;
            double x = parse_double(p);
            double res = x < 0 ? -x : x;
            print("Result: ");
            print_double(res);
            print("\n");
            continue;
        }

        if (starts_with(input, "pct ")) {
            const char* p = input + 4;
            while (*p == ' ') p++;
            double x = parse_double(p);
            while (*p && (*p >= '0' || *p == '.' || *p == '-')) p++;
            while (*p == ' ') p++;
            double total = parse_double(p);
            double res = (x * 100.0) / total;
            print("Result: ");
            print_double(res);
            print("%\n");
            continue;
        }

        if (starts_with(input, "sqrt ")) {
            const char* p = input + 5;
            while (*p == ' ') p++;
            double x = parse_double(p);

            double res = sqrt_fast(x);
            if (res < 0) {
                print("Error: sqrt of negative number\n");
            }
            else {
                print("Result: ");
                print_double(res);
                print("\n");
            }
            continue;
        }

        if (starts_with(input, "vat ")) {
            const char* p = input + 4;
            while (*p == ' ') p++;
            double x = parse_double(p);
            while (*p && (*p >= '0' || *p == '.' || *p == '-')) p++;
            while (*p == ' ') p++;
            double rate = 20.0;
            if (*p) rate = parse_double(p);
            double res = x * rate / 100.0;
            print("Result: ");
            print_double(res);
            print("\n");
            continue;
        }

        if (starts_with(input, "sum ")) {
            const char* p = input + 4;
            double total = 0;
            while (*p) {
                while (*p == ' ') p++;
                if (!(*p >= '0' || *p == '.' || *p == '-')) break;
                double x = parse_double(p);
                total += x;
                while (*p && (*p >= '0' || *p == '.' || *p == '-')) p++;
            }
            print("Result: ");
            print_double(total);
            print("\n");
            continue;
        }

        if (starts_with(input, "avg ")) {
            const char* p = input + 4;
            double total = 0;
            int count = 0;
            while (*p) {
                while (*p == ' ') p++;
                if (!(*p >= '0' || *p == '.' || *p == '-')) break;
                double x = parse_double(p);
                total += x;
                count++;
                while (*p && (*p >= '0' || *p == '.' || *p == '-')) p++;
            }
            if (count > 0) {
                double res = total / count;
                print("Result: ");
                print_double(res);
                print("\n");
            }
            continue;
        }

        if (starts_with(input, "min ")) {
            const char* p = input + 4;
            double min_val = 0;
            int first = 1;
            while (*p) {
                while (*p == ' ') p++;
                if (!(*p >= '0' || *p == '.' || *p == '-')) break;
                double x = parse_double(p);
                if (first) { min_val = x; first = 0; }
                else if (x < min_val) min_val = x;
                while (*p && (*p >= '0' || *p == '.' || *p == '-')) p++;
            }
            print("Result: ");
            print_double(min_val);
            print("\n");
            continue;
        }

        if (starts_with(input, "max ")) {
            const char* p = input + 4;
            double max_val = 0;
            int first = 1;
            while (*p) {
                while (*p == ' ') p++;
                if (!(*p >= '0' || *p == '.' || *p == '-')) break;
                double x = parse_double(p);
                if (first) { max_val = x; first = 0; }
                else if (x > max_val) max_val = x;
                while (*p && (*p >= '0' || *p == '.' || *p == '-')) p++;
            }
            print("Result: ");
            print_double(max_val);
            print("\n");
            continue;
        }

        if (starts_with(input, "bg-color ")) {
            const char* p = input + 9;
            while (*p == ' ') p++;

            int color = -1;
            if (starts_with(p, "black")) color = 0x0;
            else if (starts_with(p, "blue")) color = 0x1;
            else if (starts_with(p, "green")) color = 0x2;
            else if (starts_with(p, "cyan")) color = 0x3;
            else if (starts_with(p, "red")) color = 0x4;
            else if (starts_with(p, "magenta")) color = 0x5;
            else if (starts_with(p, "brown")) color = 0x6;
            else if (starts_with(p, "lightgray")) color = 0x7;
            else if (starts_with(p, "darkgray")) color = 0x8;
            else if (starts_with(p, "lightblue")) color = 0x9;
            else if (starts_with(p, "lightgreen")) color = 0xA;
            else if (starts_with(p, "lightcyan")) color = 0xB;
            else if (starts_with(p, "lightred")) color = 0xC;
            else if (starts_with(p, "lightmagenta")) color = 0xD;
            else if (starts_with(p, "yellow")) color = 0xE;
            else if (starts_with(p, "white")) color = 0xF;

            if (color < 0) {
                print("Unknown color\n");
                continue;
            }

            current_bg = color;

            volatile char* video = (volatile char*)VIDEO_MEMORY;
            for (int i = 0; i < 80 * 25 * 2; i += 2) {
                video[i + 1] = (video[i + 1] & 0x0F) | (current_bg << 4);
            }
            print("Background color changed\n");
            continue;
        }

        if (starts_with(input, "bg-color reset")) {
            volatile char* video = (volatile char*)VIDEO_MEMORY;
            for (int i = 0; i < 80 * 25 * 2; i += 2) {
                video[i + 1] = (video[i + 1] & 0x0F) | 0x00;
            }
            print("Background color reset to black\n");
            continue;
        }

        if (streq(input, "clear") || streq(input, "cls")) {
            clear();
            print(msg_title);
            print(msg_commands);
            print(msg_examples);
            continue;
        }

        if (streq(input, "exit")) {
            print(msg_reboot);
            for (volatile int i = 0; i < 10000000; i++);
            reboot();
        }

        if (starts_with(input, "echo ")) {
            cmd_echo(input + 5);
            continue;
        }

        if (starts_with(input, "type ")) {
            cmd_type(input + 5);
            continue;
        }

        if (starts_with(input, "print ")) {
            cmd_print(input + 6);
            continue;
        }

        if (starts_with(input, "delete ")) {
            delete_var(input + 7);
            continue;
        }

        if (streq(input, "list_vars")) {
            list_vars();
            continue;
        }

        if (starts_with(input, "char-color ")) {
            const char* p = input + 11;
            while (*p == ' ') p++;

            int color = -1;
            if (starts_with(p, "black")) color = 0x0;
            else if (starts_with(p, "blue")) color = 0x1;
            else if (starts_with(p, "green")) color = 0x2;
            else if (starts_with(p, "cyan")) color = 0x3;
            else if (starts_with(p, "red")) color = 0x4;
            else if (starts_with(p, "magenta")) color = 0x5;
            else if (starts_with(p, "brown")) color = 0x6;
            else if (starts_with(p, "lightgray")) color = 0x7;
            else if (starts_with(p, "darkgray")) color = 0x8;
            else if (starts_with(p, "lightblue")) color = 0x9;
            else if (starts_with(p, "lightgreen")) color = 0xA;
            else if (starts_with(p, "lightcyan")) color = 0xB;
            else if (starts_with(p, "lightred")) color = 0xC;
            else if (starts_with(p, "lightmagenta")) color = 0xD;
            else if (starts_with(p, "yellow")) color = 0xE;
            else if (starts_with(p, "white")) color = 0xF;

            if (color < 0) {
                print("Unknown color\n");
                continue;
            }

            current_fg = color;

            volatile char* video = (volatile char*)VIDEO_MEMORY;
            for (int i = 0; i < 80 * 25 * 2; i += 2) {
                video[i + 1] = current_fg | (current_bg << 4);
            }
            print("Text color changed\n");
            continue;
        }

        if (starts_with(input, "char-color reset")) {
            current_fg = 0x07;
            volatile char* video = (volatile char*)VIDEO_MEMORY;
            for (int i = 0; i < 80 * 25 * 2; i += 2) {
                video[i + 1] = current_fg | (current_bg << 4);
            }
            print("Text color reset to white\n");
            continue;
        }

        if (starts_with(input, "cursor-color ")) {
            const char* p = input + 13;
            while (*p == ' ') p++;

            int color = -1;
            if (starts_with(p, "black")) color = 0x0;
            else if (starts_with(p, "blue")) color = 0x1;
            else if (starts_with(p, "green")) color = 0x2;
            else if (starts_with(p, "cyan")) color = 0x3;
            else if (starts_with(p, "red")) color = 0x4;
            else if (starts_with(p, "magenta")) color = 0x5;
            else if (starts_with(p, "brown")) color = 0x6;
            else if (starts_with(p, "lightgray")) color = 0x7;
            else if (starts_with(p, "darkgray")) color = 0x8;
            else if (starts_with(p, "lightblue")) color = 0x9;
            else if (starts_with(p, "lightgreen")) color = 0xA;
            else if (starts_with(p, "lightcyan")) color = 0xB;
            else if (starts_with(p, "lightred")) color = 0xC;
            else if (starts_with(p, "lightmagenta")) color = 0xD;
            else if (starts_with(p, "yellow")) color = 0xE;
            else if (starts_with(p, "white")) color = 0xF;

            if (color < 0) {
                print("Unknown color\n");
                continue;
            }

            current_cursor = color;

            hide_cursor();
            show_cursor();
            print("Cursor color changed\n");
            continue;
        }

        if (starts_with(input, "cursor-color reset")) {
            current_cursor = 0x0F;
            hide_cursor();
            show_cursor();
            print("Cursor color reset to white\n");
            continue;
        }

        if (starts_with(input, "var ")) {

            int i = 4;

            while (input[i] == ' ') i++;

            char name[16];
            int n = 0;

            while (input[i] && input[i] != ' ' && input[i] != '=' && n < 15) {
                name[n++] = input[i++];
            }
            name[n] = 0;

            while (input[i] && input[i] != '=') i++;
            if (input[i] == '=') i++;

            while (input[i] == ' ') i++;

            set_var(name, &input[i]);
            continue;
        }

        if (streq(input, "help") || streq(input, "h")) {
            print(msg_help);
            continue;
        }

        if (strcmp(input, "time")) {
            cmd_time();
            continue;
        }

        if (is_round_command(input, &res)) {
            print(msg_result);
            print_double(res);
            print("\n");
        }
        else if (parse_and_calc_double(input, &res)) {
            print(msg_result);
            print_double(res);
            print("\n");
        }
        else {
            print(msg_error);
            print(msg_error_format);
        }
        print("\n");
    }
}