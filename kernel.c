__attribute__((section(".multiboot")))
const unsigned int multiboot_header[] = {
    0x1BADB002,
    0x00,
    -(0x1BADB002)
};

#include <stdint.h>
#include "io.h"
#include "pit.h"
#include "pic.h"
#include "idt.h"
#include "keyboard_buffer.h"

double parse_expr(const char* s, int* i);
double parse_term(const char* s, int* i);
double parse_factor(const char* s, int* i);

void hide_cursor();
void show_cursor();

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

int current_bg = 0x00; // чёрный фон по умолчанию
int current_fg = 0x07;   // белый текст по умолчанию
int current_cursor = 0x0F; // белый яркий курсор по умолчанию

// ========== СООБЩЕНИЯ ==========
const char* msg_title = "=== Bare Metal Calculator ===\n";
const char* msg_commands = "Commands: help, clear, cls, exit\n";
const char* msg_examples = "Examples: 10+5, 100-33, 6*7, 100/4, 17%%3, 2^3, sqrt(16)\n\n";
const char* msg_prompt = "> ";
const char* msg_result = "Result: ";
const char* msg_error = "Error: Invalid expression!\n";
const char* msg_error_format = "Use: number +-*/%% number or function\n";
const char* msg_reboot = "Rebooting...\n";

// Help text
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

int strcmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
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

    // имя
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

            // сдвигаем массив
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

// НОВАЯ ВЕРСИЯ read_line С ПРЕРЫВАНИЯМИ
void read_line(char* buffer, int max_len) {
    int len = 0;   // длина строки
    int pos = 0;   // курсор внутри строки

    int line_start = cursor; // запомнили начало строки

    flush_keys();  // очищаем старые нажатия

    while (1) {
        // ЖДЁМ ПРЕРЫВАНИЕ вместо busy-wait
        if (!has_key()) {
            __asm__ volatile ("hlt");
            continue;
        }

        unsigned char scancode = get_key();

        // ===== ARROWS =====
        if (scancode == 0xE0) {
            // Ждём второй скан-код через прерывания
            while (!has_key()) {
                __asm__ volatile ("hlt");
            }
            unsigned char sc2 = get_key();

            if (sc2 & 0x80) continue;

            // LEFT
            if (sc2 == 0x4B && pos > 0) {
                hide_cursor();
                pos--;
                cursor -= 2;
                show_cursor();
            }

            // RIGHT
            if (sc2 == 0x4D && pos < len) {
                hide_cursor();
                pos++;
                cursor += 2;
                show_cursor();
            }

            continue;
        }

        // SHIFT
        if (scancode == 0x2A || scancode == 0x36) { shift = 1; continue; }
        if (scancode == 0xAA || scancode == 0xB6) { shift = 0; continue; }

        // CAPS
        if (scancode == 0x3A) { caps ^= 1; continue; }

        if (scancode & 0x80) continue;

        char c = scancode_table[scancode];
        if (!c) continue;

        // SHIFT transform
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

        // ENTER
        if (c == '\n') {
            buffer[len] = '\0';
            print_char('\n');
            return;
        }

        // BACKSPACE (удаление в позиции)
        if (c == '\b') {
            if (pos > 0) {
                hide_cursor();

                for (int j = pos - 1; j < len - 1; j++)
                    buffer[j] = buffer[j + 1];

                len--;
                pos--;

                // перерисуем всю строку заново
                cursor = line_start;

                for (int i = 0; i < len; i++) {
                    volatile char* video = (volatile char*)VIDEO_MEMORY;
                    video[cursor] = buffer[i];
                    video[cursor + 1] = current_fg | (current_bg << 4);
                    cursor += 2;
                }

                // очистка хвоста
                volatile char* video = (volatile char*)VIDEO_MEMORY;
                video[cursor] = ' ';
                video[cursor + 1] = current_fg | (current_bg << 4);

                cursor = line_start + pos * 2;
                show_cursor();
            }
            continue;
        }

        // INSERT (вставка в середину)
        if (len < max_len - 1) {
            hide_cursor();

            for (int j = len; j > pos; j--)
                buffer[j] = buffer[j - 1];

            buffer[pos] = c;
            len++;
            pos++;

            // перерисовать всю строку
            cursor = line_start;

            for (int i = 0; i < len; i++) {
                volatile char* video = (volatile char*)VIDEO_MEMORY;
                video[cursor] = buffer[i];
                video[cursor + 1] = current_fg | (current_bg << 4);
                cursor += 2;
            }

            // очистка хвоста
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
    if (x < 0) return -1;  // специальное значение для ошибки
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

    // унарный минус
    if (s[*i] == '-') {
        (*i)++;
        return -parse_factor(s, i);
    }

    // скобки
    if (s[*i] == '(') {
        (*i)++;
        double val = parse_expr(s, i);
        if (s[*i] == ')') (*i)++;
        return val;
    }

    // число
    if (s[*i] >= '0' && s[*i] <= '9') {
        double val = parse_double(&s[*i]);
        while ((s[*i] >= '0' && s[*i] <= '9') || s[*i] == '.') (*i)++;
        return val;
    }

    // буква — функция или переменная
    if ((s[*i] >= 'a' && s[*i] <= 'z') || (s[*i] >= 'A' && s[*i] <= 'Z')) {
        char name[16];
        int n = 0;
        while ((s[*i] >= 'a' && s[*i] <= 'z') || (s[*i] >= 'A' && s[*i] <= 'Z')) {
            name[n++] = s[*i];
            (*i)++;
        }
        name[n] = 0;
        while (s[*i] == ' ') (*i)++;

        // переменная
        return get_var_double(name);
    }

    return 0;
}

double pow(double a, double b) {

    // 0 степень
    if (b == 0) return 1;

    // целая степень
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

    // √a (0.5 степень)
    if (b == 0.5) {
        return sqrt_fast(a);
    }

    // остальные дроби через sqrt разложение
    if (b > 0 && b < 1) {
        // приближение: a^b ≈ exp(b * ln(a)) но без ln
        double r = sqrt_fast(a);

        // грубая интерполяция
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

        // 💥 право-ассоциативность
        val = pow(val, rhs);
    }

    return val;
}

double parse_term(const char* s, int* i) {
    double val = parse_power(s, i);

    while (1) {
        while (s[*i] == ' ') (*i)++;

        // 💥 проверяем // СНАЧАЛА
        if (s[*i] == '/' && s[*i + 1] == '/') {
            (*i) += 2;
            double rhs = parse_power(s, i);

            val = (int)(val / rhs); // целочисленное деление
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

    // пропускаем пробелы
    while (input[i] == ' ') i++;

    // если остался мусор — ошибка
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

    // иногда BIOS отдаёт 12h режим
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

void kernel_main() {
    __asm__ volatile ("finit");

    pic_remap();
    idt_init();
    init_pit();

    pic_enable_irq(0);  // PIT
    pic_enable_irq(1);  // клавиатура

    __asm__ volatile ("sti");

    clear();
    show_cursor();
    print(msg_title);
    print(msg_commands);
    print(msg_examples);

    char input[64];
    double res;

    while (1) {
        print(msg_prompt);
        read_line(input, sizeof(input));

        // rch число знаки (округление до знака)
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

        // ceil число
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

        // floor число
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

        // abs число
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

        // pct число от числа
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

        // vat число [ставка]
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

        // sum число1 число2 ... (до 10 чисел)
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

        // avg число1 число2 ...
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

        // min число1 число2 ...
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

        // max число1 число2 ...
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

            current_bg = color;  // 💥 СОХРАНЯЕМ ЦВЕТ ФОНА

            volatile char* video = (volatile char*)VIDEO_MEMORY;
            for (int i = 0; i < 80 * 25 * 2; i += 2) {
                video[i + 1] = (video[i + 1] & 0x0F) | (current_bg << 4);
            }
            print("Background color changed\n");
            continue;
        }

        // bg-color reset
        if (starts_with(input, "bg-color reset")) {
            volatile char* video = (volatile char*)VIDEO_MEMORY;
            for (int i = 0; i < 80 * 25 * 2; i += 2) {
                video[i + 1] = (video[i + 1] & 0x0F) | 0x00; // чёрный фон
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

            // Обновляем уже выведенный текст на экране
            volatile char* video = (volatile char*)VIDEO_MEMORY;
            for (int i = 0; i < 80 * 25 * 2; i += 2) {
                video[i + 1] = current_fg | (current_bg << 4);
            }
            print("Text color changed\n");
            continue;
        }

        // char-color reset
        if (starts_with(input, "char-color reset")) {
            current_fg = 0x07;  // белый
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

            // Обновляем текущий курсор
            hide_cursor();
            show_cursor();
            print("Cursor color changed\n");
            continue;
        }

        // cursor-color reset
        if (starts_with(input, "cursor-color reset")) {
            current_cursor = 0x0F;  // белый яркий
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