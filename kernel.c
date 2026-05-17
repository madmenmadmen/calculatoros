__attribute__((section(".multiboot"), aligned(4)))
const unsigned int multiboot_header[] = {
    0x1BADB002,
    0x00000007,
    -(0x1BADB002 + 0x00000007),
    0, 0, 0, 0, 0,
    0,
    1024,
    768,
    32
};

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

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
} multiboot_info_t;

static void com_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x01);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static void com_putc(char c) {
    while ((inb(0x3F8 + 5) & 0x20) == 0);
    outb(0x3F8, c);
}

static void com_puts(const char* s) {
    while (*s) com_putc(*s++);
}

typedef struct block {
    int size;
    int used;
    struct block* next;
} block_t;

#define HEAP_SIZE (50 * 1024 * 1024)
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
    free_list->size = HEAP_SIZE - sizeof(block_t);
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

int shift = 0;
int cursor_visible = 1;

int caps = 0;

static uint32_t* vesa_framebuffer = NULL;
static int vesa_width = 0;
static int vesa_height = 0;
static int vesa_pitch = 0;
static int vesa_bpp = 0;

#define VESA_CONSOLE_COLS 80
#define VESA_CONSOLE_ROWS 25
#define VESA_CHAR_WIDTH 8
#define VESA_CHAR_HEIGHT 8

static int vesa_cursor_x = 0;
static int vesa_cursor_y = 0;
static uint32_t vesa_text_color = 0xFFFFFFFF;
static uint32_t vesa_bg_color = 0x00000000;

static uint32_t vesa_cursor_color = 0x00FFFFFF;

#define BIOS_FONT_ADDR ((uint8_t*)0xFFA6E)

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

static uint32_t cursor_under_pixel[8 * 8];
static char screen_buffer[VESA_CONSOLE_ROWS][VESA_CONSOLE_COLS];

int mouse_x = 400, mouse_y = 300;
int mouse_left = 0, mouse_right = 0;
int mouse_dx = 0, mouse_dy = 0;

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64

static int mouse_cycle = 0;
static uint8_t mouse_byte[3];
static int mouse_initialized = 0;

static int mouse_visible = 1;

static int input_line_len = 0;

static char input_buffer[256];
static int input_len = 0;
static int input_pos = 0;
static int input_active = 0;
static int input_line_start_x = 0;
static int input_line_start_y = 0;

typedef struct { int x, y, z; } vec3_t;
typedef struct { int x, y; } vec2_t;

static vec3_t cube_verts[8] = {
    {-50, -50,  50}, { 50, -50,  50}, { 50,  50,  50}, {-50,  50,  50},
    {-50, -50, -50}, { 50, -50, -50}, { 50,  50, -50}, {-50,  50, -50}
};

static int edges[12][2] = {
    {0,1}, {1,2}, {2,3}, {3,0},
    {4,5}, {5,6}, {6,7}, {7,4},
    {0,4}, {1,5}, {2,6}, {3,7}
};

static int faces[6][4] = {
    {0,1,2,3}, {5,4,7,6}, {4,0,3,7},
    {1,5,6,2}, {4,5,1,0}, {3,2,6,7}
};

static float cube_angle_x = 0, cube_angle_y = 0;
static int cube_center_x = 0, cube_center_y = 0;
int cube_enabled = 1;

static uint32_t* back_buffer = NULL;
static int back_buffer_size = 0;

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

void put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= vesa_width || y < 0 || y >= vesa_height) return;

    uint32_t* target = back_buffer ? back_buffer : vesa_framebuffer;

    uint32_t* row = (uint32_t*)((uint8_t*)target + y * vesa_pitch);
    row[x] = color;
}

static void init_screen_buffer(void) {
    for (int y = 0; y < VESA_CONSOLE_ROWS; y++) {
        for (int x = 0; x < VESA_CONSOLE_COLS; x++) {
            screen_buffer[y][x] = ' ';
        }
    }
}

static void screen_buffer_set(int x, int y, char c) {
    if (x >= 0 && x < VESA_CONSOLE_COLS && y >= 0 && y < VESA_CONSOLE_ROWS) {
        screen_buffer[y][x] = c;
    }
}

static char screen_buffer_get(int x, int y) {
    if (x >= 0 && x < VESA_CONSOLE_COLS && y >= 0 && y < VESA_CONSOLE_ROWS) {
        return screen_buffer[y][x];
    }
    return ' ';
}

static inline uint8_t get_font_byte(unsigned char c, int row) {

    return ((uint8_t*)0xFFA6E)[c * 8 + row];
}

void vesa_draw_char(int x, int y, char c, uint32_t color) {
    unsigned char uc = (unsigned char)c;
    if (uc < 32) uc = ' ';

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            put_pixel(x + col, y + row, vesa_bg_color);
        }
    }

    for (int row = 0; row < 8; row++) {
        uint8_t bits = get_font_byte(uc, row);
        for (int col = 0; col < 8; col++) {
            if (bits & 0x80) {
                put_pixel(x + col, y + row, color);
            }
            bits <<= 1;
        }
    }
}

void vesa_clear_char_area(int x, int y) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            put_pixel(x + col, y + row, vesa_bg_color);
        }
    }
}

void vesa_draw_cursor(void) {
    int x = vesa_cursor_x * VESA_CHAR_WIDTH;
    int y = vesa_cursor_y * VESA_CHAR_HEIGHT;

    int cursor_y = y + VESA_CHAR_HEIGHT - 1;

    uint32_t* target = back_buffer ? back_buffer : vesa_framebuffer;
    uint32_t* row = (uint32_t*)((uint8_t*)target + cursor_y * vesa_pitch);

    for (int i = 0; i < VESA_CHAR_WIDTH; i++) {
        row[x + i] = vesa_cursor_color;
    }
}

static float sinf_taylor(float x) {
    const float pi = 3.14159265f;
    const float two_pi = 6.28318531f;
    while (x > pi) x -= two_pi;
    while (x < -pi) x += two_pi;
    float x2 = x * x;
    float x4 = x2 * x2;
    return x * (1.0f - x2 / 6.0f + x4 / 120.0f - x4 * x2 / 5040.0f + x4 * x4 / 362880.0f);
}

static float cosf_taylor(float x) {
    const float pi = 3.14159265f;
    const float two_pi = 6.28318531f;
    while (x > pi) x -= two_pi;
    while (x < -pi) x += two_pi;
    float x2 = x * x;
    float x4 = x2 * x2;
    return 1.0f - x2 / 2.0f + x4 / 24.0f - x4 * x2 / 720.0f + x4 * x4 / 40320.0f;
}

static void rotate_point(vec3_t* v, float ax, float ay) {
    float x = v->x, y = v->y, z = v->z;
    float cos_ax = cosf_taylor(ax), sin_ax = sinf_taylor(ax);
    float yy = y * cos_ax - z * sin_ax;
    float zz = y * sin_ax + z * cos_ax;
    y = yy; z = zz;
    float cos_ay = cosf_taylor(ay), sin_ay = sinf_taylor(ay);
    float xx = x * cos_ay + z * sin_ay;
    float zz2 = -x * sin_ay + z * cos_ay;
    v->x = xx; v->y = y; v->z = zz2;
}

void init_double_buffer(void) {
    if (!vesa_framebuffer || vesa_width <= 0 || vesa_height <= 0) return;

    back_buffer_size = vesa_width * vesa_height * sizeof(uint32_t);

    back_buffer = malloc(back_buffer_size);

    if (back_buffer) {

        memset(back_buffer, 0x00, back_buffer_size);
        com_puts("[INFO] Double buffer initialized\n");
    }
    else {
        com_puts("[ERR] Double buffer malloc failed!\n");
    }
}

static vec2_t project(vec3_t v) {
    float scale = 300.0f / (v.z + 300.0f);
    vec2_t res;
    res.x = cube_center_x + (int)(v.x * scale);
    res.y = (cube_center_y) + (int)(v.y * scale);
    return res;
}

static void draw_line(int x1, int y1, int x2, int y2, uint32_t color) {
    int dx = x2 - x1, dy = y2 - y1;
    if (dx == 0 && dy == 0) { put_pixel(x1, y1, color); return; }
    int step_x = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
    int step_y = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
    dx = (dx > 0) ? dx : -dx;
    dy = (dy > 0) ? dy : -dy;
    int x = x1, y = y1;
    put_pixel(x, y, color);
    if (dx >= dy) {
        int error = 2 * dy - dx;
        for (int i = 0; i < dx; i++) {
            x += step_x;
            if (error >= 0) { y += step_y; error -= 2 * dx; }
            error += 2 * dy;
            put_pixel(x, y, color);
        }
    }
    else {
        int error = 2 * dx - dy;
        for (int i = 0; i < dy; i++) {
            y += step_y;
            if (error >= 0) { x += step_x; error -= 2 * dy; }
            error += 2 * dx;
            put_pixel(x, y, color);
        }
    }
}

static void fill_face(vec2_t v[4], uint32_t color) {
    int min_y = v[0].y, max_y = v[0].y;
    for (int i = 1; i < 4; i++) {
        if (v[i].y < min_y) min_y = v[i].y;
        if (v[i].y > max_y) max_y = v[i].y;
    }
    if (min_y >= vesa_height || max_y < 0) return;
    for (int y = min_y; y <= max_y; y++) {
        if (y < 0 || y >= vesa_height) continue;
        int intersections[4], inter_count = 0;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4;
            int y1 = v[i].y, y2 = v[j].y;
            if ((y1 <= y && y < y2) || (y2 <= y && y < y1)) {
                int x1 = v[i].x, x2 = v[j].x;
                intersections[inter_count++] = x1 + (y - y1) * (x2 - x1) / (y2 - y1);
            }
        }
        if (inter_count >= 2) {
            if (intersections[0] > intersections[1]) {
                int tmp = intersections[0];
                intersections[0] = intersections[1];
                intersections[1] = tmp;
            }
            int x_start = intersections[0], x_end = intersections[1];
            if (x_start < 0) x_start = 0;
            if (x_end >= vesa_width) x_end = vesa_width - 1;
            for (int x = x_start; x <= x_end; x++) put_pixel(x, y, color);
        }
    }
}

static void draw_cube(void) {
    vec3_t rotated[8];
    vec2_t screen[8];

    for (int i = 0; i < 8; i++) {
        rotated[i] = cube_verts[i];
        rotate_point(&rotated[i], cube_angle_x, cube_angle_y);
        screen[i] = project(rotated[i]);
    }

    int face_indices[6];
    float face_depths[6];
    for (int i = 0; i < 6; i++) {
        face_indices[i] = i;
        face_depths[i] = (rotated[faces[i][0]].z + rotated[faces[i][1]].z +
            rotated[faces[i][2]].z + rotated[faces[i][3]].z) / 4.0f;
    }

    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 6; j++) {
            if (face_depths[face_indices[i]] > face_depths[face_indices[j]]) {
                int tmp = face_indices[i];
                face_indices[i] = face_indices[j];
                face_indices[j] = tmp;
            }
        }
    }

    for (int i = 0; i < 6; i++) {
        int f = face_indices[i];
        vec2_t face_verts[4];
        for (int v = 0; v < 4; v++) {
            face_verts[v] = project(rotated[faces[f][v]]);
        }
        float brightness = 0.5f + (face_depths[f] + 100.0f) / 200.0f;
        if (brightness < 0.3f) brightness = 0.3f;
        if (brightness > 1.0f) brightness = 1.0f;

        uint32_t r = 100 * brightness;
        uint32_t g = 100 * brightness;
        uint32_t b = 200 * brightness;
        uint32_t face_color = (r << 16) | (g << 8) | b;
        fill_face(face_verts, face_color);
    }

    for (int i = 0; i < 12; i++) {
        vec2_t p1 = project(rotated[edges[i][0]]);
        vec2_t p2 = project(rotated[edges[i][1]]);
        draw_line(p1.x, p1.y, p2.x, p2.y, 0xFFFFFF);
    }
}

void flip_buffer(void) {
    if (!back_buffer || !vesa_framebuffer) return;

    int bytes_per_line = vesa_width * 4;

    if (vesa_pitch == bytes_per_line) {

        uint32_t* src = back_buffer;
        uint32_t* dst = vesa_framebuffer;
        int count = vesa_width * vesa_height;
        for (int i = 0; i < count; i++) {
            dst[i] = src[i];
        }
    }
    else {

        for (int y = 0; y < vesa_height; y++) {
            uint32_t* src_row = (uint32_t*)((uint8_t*)back_buffer + y * bytes_per_line);
            uint32_t* dst_row = (uint32_t*)((uint8_t*)vesa_framebuffer + y * vesa_pitch);
            for (int x = 0; x < vesa_width; x++) {
                dst_row[x] = src_row[x];
            }
        }
    }
}

void update_cube(void) {

    cube_angle_x += 0.03f;
    cube_angle_y += 0.02f;
    draw_cube();
}

void init_cube(void) {
    cube_center_x = vesa_width / 2;
    cube_center_y = vesa_height / 2;
}

void print_char(char c) {
    int pixel_x = vesa_cursor_x * 8;
    int pixel_y = vesa_cursor_y * 8;

    if (c == '\n') {
        vesa_cursor_x = 0;
        vesa_cursor_y++;
        if (vesa_cursor_y >= VESA_CONSOLE_ROWS) {
            vesa_scroll();
            vesa_cursor_y = VESA_CONSOLE_ROWS - 1;
        }
        return;
    }

    if (c == '\r') {
        vesa_cursor_x = 0;
        return;
    }

    if (c == '\b') {
        if (vesa_cursor_x > 0) {
            vesa_cursor_x--;
            screen_buffer_set(vesa_cursor_x, vesa_cursor_y, ' ');
            pixel_x = vesa_cursor_x * 8;
            pixel_y = vesa_cursor_y * 8;
            vesa_draw_char(pixel_x, pixel_y, ' ', vesa_text_color);
        }
        return;
    }

    screen_buffer_set(vesa_cursor_x, vesa_cursor_y, c);
    vesa_draw_char(pixel_x, pixel_y, c, vesa_text_color);

    vesa_cursor_x++;
    if (vesa_cursor_x >= VESA_CONSOLE_COLS) {
        vesa_cursor_x = 0;
        vesa_cursor_y++;
        if (vesa_cursor_y >= VESA_CONSOLE_ROWS) {
            vesa_scroll();
            vesa_cursor_y = VESA_CONSOLE_ROWS - 1;
        }
    }
}

void vesa_scroll(void) {

    uint32_t* target = back_buffer ? back_buffer : vesa_framebuffer;

    for (int y = 0; y < VESA_CONSOLE_ROWS - 1; y++) {
        for (int x = 0; x < VESA_CONSOLE_COLS; x++) {
            screen_buffer[y][x] = screen_buffer[y + 1][x];
        }
    }

    for (int x = 0; x < VESA_CONSOLE_COLS; x++) {
        screen_buffer[VESA_CONSOLE_ROWS - 1][x] = ' ';
    }

    int scroll_pixels = 8;
    int new_height = vesa_height - scroll_pixels;

    for (int y = 0; y < new_height; y++) {
        uint32_t* src = (uint32_t*)((uint8_t*)target + (y + scroll_pixels) * vesa_pitch);
        uint32_t* dst = (uint32_t*)((uint8_t*)target + y * vesa_pitch);
        for (int x = 0; x < vesa_width; x++) {
            dst[x] = src[x];
        }
    }

    for (int y = new_height; y < vesa_height; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)target + y * vesa_pitch);
        for (int x = 0; x < vesa_width; x++) {
            row[x] = vesa_bg_color;
        }
    }
}

void clear(void) {

    uint32_t* target = back_buffer ? back_buffer : vesa_framebuffer;

    for (int y = 0; y < vesa_height; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)target + y * vesa_pitch);
        for (int x = 0; x < vesa_width; x++) {
            row[x] = vesa_bg_color;
        }
    }

    for (int y = 0; y < VESA_CONSOLE_ROWS; y++) {
        for (int x = 0; x < VESA_CONSOLE_COLS; x++) {
            screen_buffer[y][x] = ' ';
        }
    }

    vesa_cursor_x = 0;
    vesa_cursor_y = 0;
}

void print(const char* str) {
    com_puts(str);
    while (*str) {
        print_char(*str++);
    }
}

void print_hex(uint32_t num) {
    print_char('0');
    print_char('x');

    for (int i = 28; i >= 0; i -= 4) {
        uint8_t digit = (num >> i) & 0xF;
        if (digit < 10) {
            print_char('0' + digit);
        }
        else {
            print_char('A' + (digit - 10));
        }
    }
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

void refresh_cursor() {
    int was_visible = cursor_visible;
    if (was_visible) {
        hide_cursor();
        show_cursor();
    }
}

void hide_cursor() {
    cursor_visible = 0;
}

void show_cursor() {
    cursor_visible = 1;
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

void input_start(void) {
    input_len = 0;
    input_pos = 0;
    input_active = 1;
    input_line_start_x = vesa_cursor_x;
    input_line_start_y = vesa_cursor_y;
    flush_keys();
    hide_cursor();
}

int read_line(char* buffer, int max_len) {
    if (!input_active) {
        input_start();
    }

    if (!has_key()) {
        return 0;
    }

    unsigned char scancode = get_key();

    if (scancode == 0xE0) {

        if (!has_key()) {
            return 0;
        }
        unsigned char sc2 = get_key();
        if (sc2 & 0x80) {

            return 0;
        }

        if (sc2 == 0x4B && input_pos > 0) {
            hide_cursor();
            input_pos--;
            vesa_cursor_x = input_line_start_x + input_pos;
            vesa_cursor_y = input_line_start_y;
            show_cursor();
        }
        if (sc2 == 0x4D && input_pos < input_len) {
            hide_cursor();
            input_pos++;
            vesa_cursor_x = input_line_start_x + input_pos;
            vesa_cursor_y = input_line_start_y;
            show_cursor();
        }
        return 0;
    }

    if (scancode == 0x2A || scancode == 0x36) { shift = 1; return 0; }
    if (scancode == 0xAA || scancode == 0xB6) { shift = 0; return 0; }
    if (scancode == 0x3A) { caps ^= 1; return 0; }
    if (scancode & 0x80) return 0;

    char c = scancode_table[scancode];
    if (!c) return 0;

    if (shift) {
        if ((shift ^ caps) && c >= 'a' && c <= 'z') {
            c -= 32;
        }
        else {
            switch (c) {
            case '1': c = '!'; break; case '2': c = '@'; break;
            case '3': c = '#'; break; case '4': c = '$'; break;
            case '5': c = '%'; break; case '6': c = '^'; break;
            case '7': c = '&'; break; case '8': c = '*'; break;
            case '9': c = '('; break; case '0': c = ')'; break;
            case '-': c = '_'; break; case '=': c = '+'; break;
            case '[': c = '{'; break; case ']': c = '}'; break;
            case ';': c = ':'; break; case '\'': c = '"'; break;
            case ',': c = '<'; break; case '.': c = '>'; break;
            case '/': c = '?'; break; case '\\': c = '|'; break;
            case '`': c = '~'; break;
            }
        }
    }

    if (c == '\n') {

        for (int i = 0; i < input_len && i < max_len - 1; i++) {
            buffer[i] = input_buffer[i];
        }
        buffer[input_len] = '\0';
        print_char('\n');
        input_active = 0;
        show_cursor();
        return 1;
    }

    if (c == '\b') {
        if (input_pos > 0) {
            hide_cursor();

            for (int j = input_pos - 1; j < input_len - 1; j++) {
                input_buffer[j] = input_buffer[j + 1];
            }
            input_len--;
            input_pos--;

            vesa_cursor_x = input_line_start_x;
            vesa_cursor_y = input_line_start_y;

            for (int i = 0; i < input_len; i++) {
                vesa_draw_char(vesa_cursor_x * VESA_CHAR_WIDTH,
                    vesa_cursor_y * VESA_CHAR_HEIGHT,
                    input_buffer[i], vesa_text_color);
                vesa_cursor_x++;
            }

            for (int i = input_len; i < 80; i++) {
                vesa_draw_char(vesa_cursor_x * VESA_CHAR_WIDTH,
                    vesa_cursor_y * VESA_CHAR_HEIGHT,
                    ' ', vesa_text_color);
                vesa_cursor_x++;
            }

            for (int i = 0; i < input_len; i++) {
                screen_buffer[input_line_start_y][input_line_start_x + i] = input_buffer[i];
            }
            for (int i = input_len; i < 80; i++) {
                screen_buffer[input_line_start_y][input_line_start_x + i] = ' ';
            }

            vesa_cursor_x = input_line_start_x + input_pos;
            vesa_cursor_y = input_line_start_y;
            show_cursor();
        }
        return 0;
    }

    if (input_len < max_len - 1) {
        hide_cursor();

        for (int j = input_len; j > input_pos; j--) {
            input_buffer[j] = input_buffer[j - 1];
        }
        input_buffer[input_pos] = c;
        input_len++;
        input_pos++;

        vesa_cursor_x = input_line_start_x;
        vesa_cursor_y = input_line_start_y;

        for (int i = 0; i < input_len; i++) {
            vesa_draw_char(vesa_cursor_x * VESA_CHAR_WIDTH,
                vesa_cursor_y * VESA_CHAR_HEIGHT,
                input_buffer[i], vesa_text_color);
            vesa_cursor_x++;
        }

        vesa_draw_char(vesa_cursor_x * VESA_CHAR_WIDTH,
            vesa_cursor_y * VESA_CHAR_HEIGHT,
            ' ', vesa_text_color);

        for (int i = 0; i < input_len; i++) {
            screen_buffer[input_line_start_y][input_line_start_x + i] = input_buffer[i];
        }
        for (int i = input_len; i < 80; i++) {
            screen_buffer[input_line_start_y][input_line_start_x + i] = ' ';
        }

        vesa_cursor_x = input_line_start_x + input_pos;
        vesa_cursor_y = input_line_start_y;
        show_cursor();
    }

    return 0;
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

uint32_t get_pixel(int x, int y) {
    if (x < 0 || x >= vesa_width || y < 0 || y >= vesa_height) return vesa_bg_color;
    uint32_t* row = (uint32_t*)((uint8_t*)vesa_framebuffer + y * vesa_pitch);
    return row[x];
}

void mouse_handler() {
    uint8_t data = inb(PS2_DATA_PORT);

    switch (mouse_cycle) {
    case 0:
        if ((data & 0x08) == 0) break;
        mouse_byte[0] = data;
        mouse_cycle++;
        break;
    case 1:
        mouse_byte[1] = data;
        mouse_cycle++;
        break;
    case 2:
        mouse_byte[2] = data;
        mouse_cycle = 0;

        mouse_left = mouse_byte[0] & 1;
        mouse_right = (mouse_byte[0] >> 1) & 1;

        int dx = (int8_t)mouse_byte[1];
        int dy = -(int8_t)mouse_byte[2];

        if (dx > 40) dx = 40;
        if (dx < -40) dx = -40;
        if (dy > 40) dy = 40;
        if (dy < -40) dy = -40;

        mouse_x += dx;
        mouse_y += dy;

        if (mouse_x < 0) mouse_x = 0;
        if (mouse_x >= vesa_width) mouse_x = vesa_width - 1;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_y >= vesa_height) mouse_y = vesa_height - 1;
        break;
    }
    pic_send_eoi(12);
}

void draw_mouse_cursor(void) {
    if (!mouse_visible || !back_buffer) return;

    int x = mouse_x;
    int y = mouse_y;
    int size = 8;

    if (x < 0 || y < 0 || x >= vesa_width || y >= vesa_height) return;

    uint32_t* target = back_buffer;

    for (int dy = 0; dy < size; dy++) {
        if (y + dy >= vesa_height) break;
        uint32_t* row = (uint32_t*)((uint8_t*)target + (y + dy) * vesa_pitch);
        for (int dx = 0; dx < size; dx++) {
            if (x + dx >= vesa_width) break;
            row[x + dx] = 0x00FFFFFF;
        }
    }
}

static void mouse_wait_input() {
    while (inb(PS2_STATUS_PORT) & 2);
}

static void mouse_wait_output() {
    while (!(inb(PS2_STATUS_PORT) & 1));
}

static void mouse_write(uint8_t data) {
    mouse_wait_input();
    outb(PS2_COMMAND_PORT, 0xD4);
    mouse_wait_input();
    outb(PS2_DATA_PORT, data);
}

static uint8_t mouse_read() {
    mouse_wait_output();
    return inb(PS2_DATA_PORT);
}

void init_mouse() {

    mouse_wait_input();
    outb(PS2_COMMAND_PORT, 0xA8);

    mouse_wait_input();
    outb(PS2_COMMAND_PORT, 0x20);

    mouse_wait_output();
    uint8_t status = inb(PS2_DATA_PORT);

    status |= 2;

    mouse_wait_input();
    outb(PS2_COMMAND_PORT, 0x60);

    mouse_wait_input();
    outb(PS2_DATA_PORT, status);

    mouse_write(0xF4);

    mouse_read();
}

void console_func(void) {
    char input[256];
    static int first_call = 1;

    if (first_call) {
        print_prompt();
        first_call = 0;
    }

    if (read_line(input, sizeof(input))) {

        if (starts_with(input, "cube on")) {
            cube_enabled = 1;
            print("Cube enabled\n");
        }
        else if (starts_with(input, "cube off")) {
            cube_enabled = 0;
            print("Cube disabled\n");
        }
        else if (starts_with(input, "size ")) {
            int sz = fs_get_file_size(input + 5);
            if (sz < 0) print("Not found\n");
            else {
                print("Size: ");
                print_hex(sz);
                print(" bytes\n");
            }
        }
        else if (starts_with(input, "append ")) {
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
                }
                else {
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
            }
        }
        else if (starts_with(input, "cp ")) {
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
                }
                else {
                    char* buf = (char*)malloc(size + 1);
                    if (buf) {
                        int read_size = fs_read_file_to_buf(src, buf, size);
                        if (read_size < 0) {
                            print("Read failed\n");
                        }
                        else {
                            buf[read_size] = 0;
                            if (!fs_create_file(p)) {
                                print("Cannot create destination\n");
                            }
                            else if (fs_write_file(p, buf)) {
                                print("Copied\n");
                            }
                            else {
                                print("Write failed\n");
                            }
                        }
                        free(buf);
                    }
                    else {
                        print("Out of memory\n");
                    }
                }
            }
            else {
                print("Usage: cp <source> <dest>\n");
            }
        }
        else if (starts_with(input, "mv ")) {
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
                }
                else {
                    char* buf = (char*)malloc(size + 1);
                    if (buf) {
                        int read_size = fs_read_file_to_buf(src, buf, size);
                        if (read_size < 0) {
                            print("Read failed\n");
                        }
                        else {
                            buf[read_size] = 0;
                            if (!fs_create_file(p)) {
                                print("Cannot create destination\n");
                            }
                            else if (fs_write_file(p, buf)) {
                                fs_remove_file(src);
                                print("Moved\n");
                            }
                            else {
                                print("Write failed\n");
                            }
                        }
                        free(buf);
                    }
                    else {
                        print("Out of memory\n");
                    }
                }
            }
            else {
                print("Usage: mv <source> <dest>\n");
            }
        }
        else if (strncmp(input, "mount ", 6) == 0) {
            cmd_mount(input);
        }
        else if (starts_with(input, "symlink ")) {
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
        }
        else if (starts_with(input, "readlink ")) {
            vfs_readlink(input + 9);
        }
        else if (streq(input, "showpart") || streq(input, "showpartitions")) {
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
        }
        else if (starts_with(input, "delpart ")) {
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
            }

            if (part_num >= 1 && part_num <= 4) {
                block_device_t* dev = block_find(devname + 5);
                if (!dev || !dev->present) {
                    print("Device not found\n");
                }
                else if (delete_mbr_partition(dev, part_num)) {
                    print("Partition deleted\n");
                    fs_remove_device(full_devname);
                    fs_update_devices();
                }
                else {
                    print("Delete failed\n");
                }
            }
            else {
                print("Partition number must be 1-4\n");
            }
        }
        else if (starts_with(input, "mkpart ")) {
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

            if (part_num >= 1 && part_num <= 4 && size_mb >= 1) {
                block_device_t* dev = block_find(devname + 5);
                if (!dev || !dev->present) {
                    print("Device not found\n");
                }
                else {
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
                }
            }
            else {
                print("Usage: mkpart /dev/hda1 100\n");
            }
        }
        else if (starts_with(input, "umount ")) {
            char mntpoint[MAX_PATH];
            int i = 7;
            while (input[i] == ' ') i++;
            int j = 0;
            while (input[i] && input[i] != ' ' && j < MAX_PATH - 1) {
                mntpoint[j++] = input[i++];
            }
            mntpoint[j] = 0;
            fs_umount(mntpoint);
        }
        else if (starts_with(input, "rm ")) {
            if (!vfs_remove_file(input + 3))
                print("Failed\n");
        }
        else if (strncmp(input, "format ", 7) == 0) {
            const char* path = input + 7;
            if (fs_format_device(path)) {
                print("Formatted ");
                print(path);
                print("\n");
            }
            else {
                print("Format failed\n");
            }
        }
        else if (starts_with(input, "rmdir ")) {
            if (!vfs_remove_dir(input + 6))
                print("Failed\n");
        }
        else if (starts_with(input, "new ")) {
            if (!vfs_create_file(input + 4))
                print("Failed\n");
        }
        else if (starts_with(input, "newdir ")) {
            if (!vfs_create_dir(input + 7))
                print("Failed\n");
        }
        else if (starts_with(input, "write ")) {
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
        }
        else if (starts_with(input, "cat ")) {
            vfs_read(input + 4);
        }
        else if (starts_with(input, "ls ")) {
            vfs_list(input + 3);
        }
        else if (starts_with(input, "dir ")) {
            vfs_list(input + 4);
        }
        else if (starts_with(input, "cd ")) {
            vfs_cd(input + 3);
        }
        else if (streq(input, "pwd")) {
            fs_pwd();
        }
        else if (streq(input, "ls") || streq(input, "dir")) {
            vfs_list("");
        }
        else if (starts_with(input, "rch ")) {
            const char* p = input + 4;
            while (*p == ' ') p++;
            double x = parse_double(p);
            while (*p && (*p >= '0' || *p == '.' || *p == '-')) p++;
            while (*p == ' ') p++;
            double n = parse_double(p);

            double mult = 1.0;
            for (int i = 0; i < (int)n; i++) mult *= 10.0;
            double res_val = (double)(int)(x * mult + (x > 0 ? 0.5 : -0.5)) / mult;
            print("Result: ");
            print_double(res_val);
            print("\n");
        }
        else if (starts_with(input, "asm ")) {
            const char* asm_code = input + 4;
            if (!assemble_and_execute(asm_code)) {
                print("Failed to assemble/execute\n");
            }
        }
        else if (starts_with(input, "ceil ")) {
            const char* p = input + 5;
            while (*p == ' ') p++;
            double x = parse_double(p);
            double res_val = (double)(int)(x + 0.999999);
            print("Result: ");
            print_double(res_val);
            print("\n");
        }
        else if (starts_with(input, "floor ")) {
            const char* p = input + 6;
            while (*p == ' ') p++;
            double x = parse_double(p);
            double res_val = (double)(int)x;
            print("Result: ");
            print_double(res_val);
            print("\n");
        }
        else if (starts_with(input, "abs ")) {
            const char* p = input + 4;
            while (*p == ' ') p++;
            double x = parse_double(p);
            double res_val = x < 0 ? -x : x;
            print("Result: ");
            print_double(res_val);
            print("\n");
        }
        else if (starts_with(input, "pct ")) {
            const char* p = input + 4;
            while (*p == ' ') p++;
            double x = parse_double(p);
            while (*p && (*p >= '0' || *p == '.' || *p == '-')) p++;
            while (*p == ' ') p++;
            double total = parse_double(p);
            double res_val = (x * 100.0) / total;
            print("Result: ");
            print_double(res_val);
            print("%\n");
        }
        else if (starts_with(input, "sqrt ")) {
            const char* p = input + 5;
            while (*p == ' ') p++;
            double x = parse_double(p);
            double res_val = sqrt_fast(x);
            if (res_val < 0) {
                print("Error: sqrt of negative number\n");
            }
            else {
                print("Result: ");
                print_double(res_val);
                print("\n");
            }
        }
        else if (starts_with(input, "vat ")) {
            const char* p = input + 4;
            while (*p == ' ') p++;
            double x = parse_double(p);
            while (*p && (*p >= '0' || *p == '.' || *p == '-')) p++;
            while (*p == ' ') p++;
            double rate = 20.0;
            if (*p) rate = parse_double(p);
            double res_val = x * rate / 100.0;
            print("Result: ");
            print_double(res_val);
            print("\n");
        }
        else if (starts_with(input, "sum ")) {
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
        }
        else if (starts_with(input, "avg ")) {
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
                double res_val = total / count;
                print("Result: ");
                print_double(res_val);
                print("\n");
            }
        }
        else if (starts_with(input, "min ")) {
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
        }
        else if (starts_with(input, "max ")) {
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
        }
        else if (starts_with(input, "bg-color reset")) {
            vesa_bg_color = 0x00000000;
            for (int y = 0; y < vesa_height; y++) {
                uint32_t* row = (uint32_t*)((uint8_t*)vesa_framebuffer + y * vesa_pitch);
                for (int x = 0; x < vesa_width; x++) {
                    row[x] = vesa_bg_color;
                }
            }
            for (int y = 0; y < VESA_CONSOLE_ROWS; y++) {
                for (int x = 0; x < VESA_CONSOLE_COLS; x++) {
                    char c = screen_buffer[y][x];
                    if (c != ' ') {
                        int pixel_x = x * 8;
                        int pixel_y = y * 8;
                        vesa_draw_char(pixel_x, pixel_y, c, vesa_text_color);
                    }
                }
            }
            refresh_cursor();
            print("Background color reset to black\n");
        }
        else if (starts_with(input, "bg-color ")) {
            const char* p = input + 9;
            while (*p == ' ') p++;

            uint32_t new_bg = 0x00000000;
            if (starts_with(p, "black")) new_bg = 0x00000000;
            else if (starts_with(p, "blue")) new_bg = 0x000000FF;
            else if (starts_with(p, "green")) new_bg = 0x0000FF00;
            else if (starts_with(p, "cyan")) new_bg = 0x0000FFFF;
            else if (starts_with(p, "red")) new_bg = 0x00FF0000;
            else if (starts_with(p, "magenta")) new_bg = 0x00FF00FF;
            else if (starts_with(p, "yellow")) new_bg = 0x00FFFF00;
            else if (starts_with(p, "white")) new_bg = 0x00FFFFFF;
            else {
                print("Unknown color\n");
            }

            if (new_bg != vesa_bg_color) {
                vesa_bg_color = new_bg;
                for (int y = 0; y < vesa_height; y++) {
                    uint32_t* row = (uint32_t*)((uint8_t*)vesa_framebuffer + y * vesa_pitch);
                    for (int x = 0; x < vesa_width; x++) {
                        row[x] = vesa_bg_color;
                    }
                }
                for (int y = 0; y < VESA_CONSOLE_ROWS; y++) {
                    for (int x = 0; x < VESA_CONSOLE_COLS; x++) {
                        char c = screen_buffer[y][x];
                        if (c != ' ') {
                            int pixel_x = x * 8;
                            int pixel_y = y * 8;
                            vesa_draw_char(pixel_x, pixel_y, c, vesa_text_color);
                        }
                    }
                }
                refresh_cursor();
                print("Background color changed\n");
            }
        }
        else if (starts_with(input, "char-color reset")) {
            vesa_text_color = 0x00FFFFFF;
            for (int y = 0; y < VESA_CONSOLE_ROWS; y++) {
                for (int x = 0; x < VESA_CONSOLE_COLS; x++) {
                    char c = screen_buffer[y][x];
                    if (c != ' ') {
                        int pixel_x = x * 8;
                        int pixel_y = y * 8;
                        vesa_draw_char(pixel_x, pixel_y, c, vesa_text_color);
                    }
                }
            }
            refresh_cursor();
            print("Text color reset to white\n");
        }
        else if (starts_with(input, "char-color ")) {
            const char* p = input + 11;
            while (*p == ' ') p++;

            uint32_t new_color = 0xFFFFFFFF;
            if (starts_with(p, "black")) new_color = 0x00000000;
            else if (starts_with(p, "blue")) new_color = 0x000000FF;
            else if (starts_with(p, "green")) new_color = 0x0000FF00;
            else if (starts_with(p, "cyan")) new_color = 0x0000FFFF;
            else if (starts_with(p, "red")) new_color = 0x00FF0000;
            else if (starts_with(p, "magenta")) new_color = 0x00FF00FF;
            else if (starts_with(p, "yellow")) new_color = 0x00FFFF00;
            else if (starts_with(p, "white")) new_color = 0x00FFFFFF;
            else {
                print("Unknown color\n");
            }

            if (new_color != vesa_text_color) {
                vesa_text_color = new_color;
                for (int y = 0; y < VESA_CONSOLE_ROWS; y++) {
                    for (int x = 0; x < VESA_CONSOLE_COLS; x++) {
                        char c = screen_buffer[y][x];
                        if (c != ' ') {
                            int pixel_x = x * 8;
                            int pixel_y = y * 8;
                            vesa_draw_char(pixel_x, pixel_y, c, vesa_text_color);
                        }
                    }
                }
                refresh_cursor();
                print("Text color changed\n");
            }
        }
        else if (starts_with(input, "cursor-color reset")) {
            vesa_cursor_color = 0x00FFFFFF;
            refresh_cursor();
            print("Cursor color reset to white\n");
        }
        else if (starts_with(input, "cursor-color ")) {
            const char* p = input + 13;
            while (*p == ' ') p++;

            uint32_t new_color = 0x00FFFFFF;
            if (starts_with(p, "black")) new_color = 0x00000000;
            else if (starts_with(p, "blue")) new_color = 0x000000FF;
            else if (starts_with(p, "green")) new_color = 0x0000FF00;
            else if (starts_with(p, "cyan")) new_color = 0x0000FFFF;
            else if (starts_with(p, "red")) new_color = 0x00FF0000;
            else if (starts_with(p, "magenta")) new_color = 0x00FF00FF;
            else if (starts_with(p, "yellow")) new_color = 0x00FFFF00;
            else if (starts_with(p, "white")) new_color = 0x00FFFFFF;
            else {
                print("Unknown color\n");
            }

            if (new_color != vesa_cursor_color) {
                vesa_cursor_color = new_color;
                refresh_cursor();
                print("Cursor color changed\n");
            }
        }
        else if (streq(input, "clear") || streq(input, "cls")) {
            clear();
            print(msg_title);
            print(msg_commands);
            print(msg_examples);
        }
        else if (streq(input, "exit")) {
            print(msg_reboot);
            for (volatile int i = 0; i < 10000000; i++);
            reboot();
        }
        else if (starts_with(input, "echo ")) {
            cmd_echo(input + 5);
        }
        else if (starts_with(input, "type ")) {
            cmd_type(input + 5);
        }
        else if (starts_with(input, "print ")) {
            cmd_print(input + 6);
        }
        else if (starts_with(input, "delete ")) {
            delete_var(input + 7);
        }
        else if (streq(input, "list_vars")) {
            list_vars();
        }
        else if (starts_with(input, "var ")) {
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
        }
        else if (streq(input, "help") || streq(input, "h")) {
            print(msg_help);
        }
        else if (strcmp(input, "time")) {
            cmd_time();
        }
        else {
            double calc_res;
            if (is_round_command(input, &calc_res)) {
                print(msg_result);
                print_double(calc_res);
                print("\n");
            }
            else if (parse_and_calc_double(input, &calc_res)) {
                print(msg_result);
                print_double(calc_res);
                print("\n");
            }
            else {
                print(msg_error);
                print(msg_error_format);
            }
        }
        print("\n");

        print_prompt();
    }
}

void update(void) {
    if (!back_buffer) return;

    uint32_t bg = vesa_bg_color;
    int total = vesa_width * vesa_height;
    for (int i = 0; i < total; i++) {
        back_buffer[i] = bg;
    }

    for (int y = 0; y < VESA_CONSOLE_ROWS; y++) {
        for (int x = 0; x < VESA_CONSOLE_COLS; x++) {
            char c = screen_buffer[y][x];
            if (c != ' ') {
                vesa_draw_char(x * VESA_CHAR_WIDTH, y * VESA_CHAR_HEIGHT, c, vesa_text_color);
            }
        }
    }
}

void kernel_main(uint32_t magic, multiboot_info_t* mb_info) {
    com_init();

    vesa_framebuffer = (uint32_t*)mb_info->framebuffer_addr;
    vesa_width = mb_info->framebuffer_width;
    vesa_height = mb_info->framebuffer_height;
    vesa_pitch = mb_info->framebuffer_pitch;
    vesa_bpp = mb_info->framebuffer_bpp;

    mouse_x = vesa_width / 2;
    mouse_y = vesa_height / 2;

    init_screen_buffer();

    __asm__ volatile ("finit");

    init_phys();
    init_heap();

    pic_remap();
    idt_init();
    init_pit();

    init_mouse();

    pic_enable_irq(0);
    pic_enable_irq(1);
    pic_enable_irq(2);
    pic_enable_irq(12);

    clear();

    pci_init();
    block_init();
    fs_init();

    init_cube();
    init_double_buffer();

    __asm__ volatile ("sti");

    show_cursor();

    print(msg_title);
    print(msg_commands);
    print(msg_examples);

    while (1) {

        console_func();
    }
}