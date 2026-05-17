#include <stdint.h>

extern void print(const char*);
extern void print_hex(uint32_t);
extern int strncmp(const char*, const char*, int);

static uint32_t jit_offset = 0;

__attribute__((section(".text")))
static uint8_t jit_pool[4096] __attribute__((aligned(16)));

static void emit_byte(uint8_t* code, int* len, uint8_t b) {
    code[(*len)++] = b;
}

static void emit_dword(uint8_t* code, int* len, uint32_t d) {
    code[(*len)++] = d & 0xFF;
    code[(*len)++] = (d >> 8) & 0xFF;
    code[(*len)++] = (d >> 16) & 0xFF;
    code[(*len)++] = (d >> 24) & 0xFF;
}

static void emit_word(uint8_t* code, int* len, uint16_t w) {
    code[(*len)++] = w & 0xFF;
    code[(*len)++] = (w >> 8) & 0xFF;
}

static int parse_reg(const char* s, int* pos) {
    if (!strncmp(&s[*pos], "xmm0", 4)) { *pos += 4; return 0; }
    if (!strncmp(&s[*pos], "xmm1", 4)) { *pos += 4; return 1; }
    if (!strncmp(&s[*pos], "xmm2", 4)) { *pos += 4; return 2; }
    if (!strncmp(&s[*pos], "xmm3", 4)) { *pos += 4; return 3; }
    if (!strncmp(&s[*pos], "xmm4", 4)) { *pos += 4; return 4; }
    if (!strncmp(&s[*pos], "xmm5", 4)) { *pos += 4; return 5; }
    if (!strncmp(&s[*pos], "xmm6", 4)) { *pos += 4; return 6; }
    if (!strncmp(&s[*pos], "xmm7", 4)) { *pos += 4; return 7; }

    if (!strncmp(&s[*pos], "eax", 3)) { *pos += 3; return 0; }
    if (!strncmp(&s[*pos], "ecx", 3)) { *pos += 3; return 1; }
    if (!strncmp(&s[*pos], "edx", 3)) { *pos += 3; return 2; }
    if (!strncmp(&s[*pos], "ebx", 3)) { *pos += 3; return 3; }
    if (!strncmp(&s[*pos], "esp", 3)) { *pos += 3; return 4; }
    if (!strncmp(&s[*pos], "ebp", 3)) { *pos += 3; return 5; }
    if (!strncmp(&s[*pos], "esi", 3)) { *pos += 3; return 6; }
    if (!strncmp(&s[*pos], "edi", 3)) { *pos += 3; return 7; }
    return -1;
}

uint32_t parse_dec(const char* s) {
    uint32_t res = 0;
    int i = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        res = res * 10 + (s[i] - '0');
        i++;
    }
    return res;
}

uint32_t parse_hex(const char* s) {
    uint32_t res = 0;
    int i = 0;
    while (1) {
        char c = s[i];
        uint32_t val;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else break;
        res = (res << 4) | val;
        i++;
    }
    return res;
}

uint32_t jit_call(void* fn) {
    uint32_t result;
    __asm__ volatile (
        "call *%1\n"
        : "=a"(result)
        : "r"(fn)
        );
    return result;
}

int assemble_and_execute(const char* asm_line) {

    if (jit_offset + 128 >= sizeof(jit_pool)) {
        print("JIT reset\n");
        jit_offset = 0;
    }

    jit_offset = (jit_offset + 15) & ~15;

    uint8_t* code = &jit_pool[jit_offset];
    int len = 0;
    int pos = 0;

    while (asm_line[pos] == ' ') pos++;

    if (!strncmp(&asm_line[pos], "nop", 3)) {
        emit_byte(code, &len, 0x90);
    }

    else if (!strncmp(&asm_line[pos], "ret", 3)) {
        emit_byte(code, &len, 0xC3);
    }

    else if (!strncmp(&asm_line[pos], "ud2", 3)) {
        emit_byte(code, &len, 0x0F);
        emit_byte(code, &len, 0x0B);
    }

    else if (!strncmp(&asm_line[pos], "int ", 4)) {
        pos += 4;
        uint8_t vector = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
            ? (uint8_t)parse_hex(asm_line + pos + 2)
            : (uint8_t)parse_dec(asm_line + pos);
        emit_byte(code, &len, 0xCD);
        emit_byte(code, &len, vector);
    }

    else if (!strncmp(&asm_line[pos], "int1", 4)) {
        emit_byte(code, &len, 0xCD);
        emit_byte(code, &len, 0x01);
    }

    else if (!strncmp(&asm_line[pos], "int3", 4)) {
        emit_byte(code, &len, 0xCC);
    }

    else if (!strncmp(&asm_line[pos], "into", 4)) {
        emit_byte(code, &len, 0xCE);
    }

    else if (!strncmp(&asm_line[pos], "icebp", 5)) {
        emit_byte(code, &len, 0xF1);
    }

    else if (!strncmp(&asm_line[pos], "popf", 4)) {
        emit_byte(code, &len, 0x9D);
    }

    else if (!strncmp(&asm_line[pos], "pushf", 5)) {
        emit_byte(code, &len, 0x9C);
    }

    else if (!strncmp(&asm_line[pos], "retf", 4)) {
        emit_byte(code, &len, 0xCB);
    }

    else if (!strncmp(&asm_line[pos], "iret", 4)) {
        emit_byte(code, &len, 0xCF);
    }

    else if (!strncmp(&asm_line[pos], "lcall ", 6)) {
        pos += 6;
        uint32_t offset = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
            ? parse_hex(asm_line + pos + 2)
            : parse_dec(asm_line + pos);
        while (asm_line[pos] && asm_line[pos] != ':') pos++;
        if (asm_line[pos] == ':') pos++;
        uint16_t selector = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
            ? (uint16_t)parse_hex(asm_line + pos + 2)
            : (uint16_t)parse_dec(asm_line + pos);

        emit_byte(code, &len, 0x9A);
        emit_dword(code, &len, offset);
        emit_word(code, &len, selector);
    }

    else if (!strncmp(&asm_line[pos], "lret", 4)) {
        emit_byte(code, &len, 0xCB);
    }

    else if (!strncmp(&asm_line[pos], "mov cr0, ", 9)) {
        pos += 9;
        int r = parse_reg(asm_line, &pos);
        if (r < 0) return 0;

        emit_byte(code, &len, 0x0F);
        emit_byte(code, &len, 0x22);
        emit_byte(code, &len, 0xC0 | (r << 3));
    }

    else if (!strncmp(&asm_line[pos], "mov eax, cr0", 12)) {
        emit_byte(code, &len, 0x0F);
        emit_byte(code, &len, 0x20);
        emit_byte(code, &len, 0xC0);
    }

    else if (!strncmp(&asm_line[pos], "lgdt ", 5)) {
        pos += 5;
        uint32_t addr = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
            ? parse_hex(asm_line + pos + 2)
            : parse_dec(asm_line + pos);

        emit_byte(code, &len, 0x0F);
        emit_byte(code, &len, 0x01);
        emit_byte(code, &len, 0x15);
        emit_dword(code, &len, addr);
    }

    else if (!strncmp(&asm_line[pos], "lidt ", 5)) {
        pos += 5;
        uint32_t addr = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
            ? parse_hex(asm_line + pos + 2)
            : parse_dec(asm_line + pos);

        emit_byte(code, &len, 0x0F);
        emit_byte(code, &len, 0x01);
        emit_byte(code, &len, 0x1D);
        emit_dword(code, &len, addr);
    }

    else if (!strncmp(&asm_line[pos], "retf ", 5)) {
        pos += 5;
        uint16_t imm = (uint16_t)parse_dec(asm_line + pos);

        emit_byte(code, &len, 0xCA);
        emit_word(code, &len, imm);
    }

    else if (!strncmp(&asm_line[pos], "xor ax, ax", 10)) {
        emit_byte(code, &len, 0x31);
        emit_byte(code, &len, 0xC0);
    }

    else if (!strncmp(&asm_line[pos], "mov ds, ax", 10)) {
        emit_byte(code, &len, 0x8E);
        emit_byte(code, &len, 0xD8);
    }

    else if (!strncmp(&asm_line[pos], "mov es, ax", 10)) {
        emit_byte(code, &len, 0x8E);
        emit_byte(code, &len, 0xC0);
    }

    else if (!strncmp(&asm_line[pos], "mov fs, ax", 10)) {
        emit_byte(code, &len, 0x8E);
        emit_byte(code, &len, 0xE0);
    }

    else if (!strncmp(&asm_line[pos], "mov gs, ax", 10)) {
        emit_byte(code, &len, 0x8E);
        emit_byte(code, &len, 0xE8);
    }

    else if (!strncmp(&asm_line[pos], "mov ss, ax", 10)) {
        emit_byte(code, &len, 0x8E);
        emit_byte(code, &len, 0xD0);
    }

    else if (!strncmp(&asm_line[pos], "mov dr0, ", 9)) {
        pos += 9;
        int r = parse_reg(asm_line, &pos);
        if (r < 0) return 0;
        emit_byte(code, &len, 0x0F);
        emit_byte(code, &len, 0x23);
        emit_byte(code, &len, 0xC0 | (r << 3) | 0);
    }

    else if (!strncmp(&asm_line[pos], "mov dr1, ", 9)) {
        pos += 9;
        int r = parse_reg(asm_line, &pos);
        if (r < 0) return 0;
        emit_byte(code, &len, 0x0F);
        emit_byte(code, &len, 0x23);
        emit_byte(code, &len, 0xC8 | (r << 3) | 0);
    }

    else if (!strncmp(&asm_line[pos], "push ", 5)) {
        pos += 5;
        int r = parse_reg(asm_line, &pos);
        if (r < 0) return 0;
        emit_byte(code, &len, 0x50 + r);
    }

    else if (!strncmp(&asm_line[pos], "push ", 5) && (asm_line[pos + 5] >= '0' || asm_line[pos + 5] == '0' && asm_line[pos + 6] == 'x')) {
        pos += 5;
        uint32_t val = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
            ? parse_hex(asm_line + pos + 2)
            : parse_dec(asm_line + pos);
        emit_byte(code, &len, 0x68);
        emit_dword(code, &len, val);
    }

    else if (!strncmp(&asm_line[pos], "pop ", 4)) {
        pos += 4;
        int r = parse_reg(asm_line, &pos);
        if (r < 0) return 0;
        emit_byte(code, &len, 0x58 + r);
    }

    else if (!strncmp(&asm_line[pos], "mov [", 5)) {
        pos += 5;
        uint32_t addr = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
            ? parse_hex(asm_line + pos + 2)
            : parse_dec(asm_line + pos);
        while (asm_line[pos] && asm_line[pos] != ']') pos++;
        if (asm_line[pos] == ']') pos++;
        while (asm_line[pos] == ' ' || asm_line[pos] == ',') pos++;
        int r = parse_reg(asm_line, &pos);
        if (r < 0) return 0;
        emit_byte(code, &len, 0xB8);
        emit_dword(code, &len, addr);
        emit_byte(code, &len, 0x89);
        emit_byte(code, &len, 0x00 | (r << 3) | 0);
    }

    else if (!strncmp(&asm_line[pos], "mov ", 4)) {
        int saved_pos = pos;
        pos += 4;
        int r1 = parse_reg(asm_line, &pos);
        if (r1 < 0) return 0;
        while (asm_line[pos] == ' ' || asm_line[pos] == ',') pos++;
        if (asm_line[pos] == '[') {
            pos++;
            uint32_t addr = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
                ? parse_hex(asm_line + pos + 2)
                : parse_dec(asm_line + pos);
            emit_byte(code, &len, 0xB8);
            emit_dword(code, &len, addr);
            emit_byte(code, &len, 0x8B);
            emit_byte(code, &len, 0x00 | (r1 << 3) | 0);
        }
        else {
            pos = saved_pos + 4;
            int r1b = parse_reg(asm_line, &pos);
            while (asm_line[pos] == ' ' || asm_line[pos] == ',') pos++;
            int save = pos;
            int r2 = parse_reg(asm_line, &pos);
            if (r2 >= 0) {
                emit_byte(code, &len, 0x89);
                emit_byte(code, &len, 0xC0 | (r2 << 3) | r1b);
            }
            else {
                pos = save;
                uint32_t val = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
                    ? parse_hex(asm_line + pos + 2)
                    : parse_dec(asm_line + pos);
                emit_byte(code, &len, 0xB8 + r1b);
                emit_dword(code, &len, val);
            }
        }
    }

    else if (!strncmp(&asm_line[pos], "add ", 4)) {
        pos += 4;
        int r1 = parse_reg(asm_line, &pos);
        if (r1 < 0) return 0;
        while (asm_line[pos] == ' ' || asm_line[pos] == ',') pos++;
        int save = pos;
        int r2 = parse_reg(asm_line, &pos);
        if (r2 >= 0) {
            emit_byte(code, &len, 0x01);
            emit_byte(code, &len, 0xC0 | (r2 << 3) | r1);
        }
        else {
            pos = save;
            uint32_t imm = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
                ? parse_hex(asm_line + pos + 2)
                : parse_dec(asm_line + pos);
            emit_byte(code, &len, 0x81);
            emit_byte(code, &len, 0xC0 | r1);
            emit_dword(code, &len, imm);
        }
    }

    else if (!strncmp(&asm_line[pos], "sub ", 4)) {
        pos += 4;
        int r1 = parse_reg(asm_line, &pos);
        if (r1 < 0) return 0;
        while (asm_line[pos] == ' ' || asm_line[pos] == ',') pos++;
        int save = pos;
        int r2 = parse_reg(asm_line, &pos);
        if (r2 >= 0) {
            emit_byte(code, &len, 0x29);
            emit_byte(code, &len, 0xC0 | (r2 << 3) | r1);
        }
        else {
            pos = save;
            uint32_t imm = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
                ? parse_hex(asm_line + pos + 2)
                : parse_dec(asm_line + pos);
            emit_byte(code, &len, 0x81);
            emit_byte(code, &len, 0xE8 | r1);
            emit_dword(code, &len, imm);
        }
    }

    else if (!strncmp(&asm_line[pos], "cmp ", 4)) {
        pos += 4;
        int r1 = parse_reg(asm_line, &pos);
        while (asm_line[pos] == ' ' || asm_line[pos] == ',') pos++;
        int r2 = parse_reg(asm_line, &pos);
        if (r1 < 0 || r2 < 0) return 0;
        emit_byte(code, &len, 0x39);
        emit_byte(code, &len, 0xC0 | (r2 << 3) | r1);
    }

    else if (!strncmp(&asm_line[pos], "je ", 3)) {
        pos += 3;
        int8_t offset = (int8_t)parse_dec(asm_line + pos);
        emit_byte(code, &len, 0x74);
        emit_byte(code, &len, offset);
    }

    else if (!strncmp(&asm_line[pos], "jne ", 4)) {
        pos += 4;
        int8_t offset = (int8_t)parse_dec(asm_line + pos);
        emit_byte(code, &len, 0x75);
        emit_byte(code, &len, offset);
    }

    else if (!strncmp(&asm_line[pos], "inc ", 4)) {
        pos += 4;
        int r = parse_reg(asm_line, &pos);
        if (r < 0) return 0;
        emit_byte(code, &len, 0x40 + r);
    }

    else if (!strncmp(&asm_line[pos], "dec ", 4)) {
        pos += 4;
        int r = parse_reg(asm_line, &pos);
        if (r < 0) return 0;
        emit_byte(code, &len, 0x48 + r);
    }

    else if (!strncmp(&asm_line[pos], "div ", 4)) {
        pos += 4;
        int r = parse_reg(asm_line, &pos);
        if (r < 0) return 0;
        emit_byte(code, &len, 0xF7);
        emit_byte(code, &len, 0xF0 | r);
    }

    else if (!strncmp(&asm_line[pos], "idiv ", 5)) {
        pos += 5;
        int r = parse_reg(asm_line, &pos);
        if (r < 0) return 0;
        emit_byte(code, &len, 0xF7);
        emit_byte(code, &len, 0xF8 | r);
    }

    else if (!strncmp(&asm_line[pos], "jmp ", 4)) {
        pos += 4;
        uint32_t addr = parse_dec(asm_line + pos);
        emit_byte(code, &len, 0xB8);
        emit_dword(code, &len, addr);
        emit_byte(code, &len, 0xFF);
        emit_byte(code, &len, 0xE0);
    }

    else if (!strncmp(&asm_line[pos], "jmpf ", 5)) {
        pos += 5;
        uint32_t offset = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
            ? parse_hex(asm_line + pos + 2)
            : parse_dec(asm_line + pos);
        while (asm_line[pos] && asm_line[pos] != ':') pos++;
        if (asm_line[pos] == ':') pos++;
        uint16_t selector = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
            ? (uint16_t)parse_hex(asm_line + pos + 2)
            : (uint16_t)parse_dec(asm_line + pos);
        emit_byte(code, &len, 0xEA);
        emit_dword(code, &len, offset);
        emit_word(code, &len, selector);
    }

    else if (!strncmp(&asm_line[pos], "addof ", 6)) {
        pos += 6;
        int r = parse_reg(asm_line, &pos);
        if (r < 0) return 0;
        while (asm_line[pos] == ' ' || asm_line[pos] == ',') pos++;
        uint32_t imm = (asm_line[pos] == '0' && asm_line[pos + 1] == 'x')
            ? parse_hex(asm_line + pos + 2)
            : parse_dec(asm_line + pos);
        emit_byte(code, &len, 0x81);
        emit_byte(code, &len, 0xC0 | r);
        emit_dword(code, &len, imm);
        emit_byte(code, &len, 0xCE);
    }

    else if (!strncmp(&asm_line[pos], "divss ", 6)) {
        pos += 6;
        int r1 = parse_reg(asm_line, &pos);
        while (asm_line[pos] == ' ' || asm_line[pos] == ',') pos++;
        int r2 = parse_reg(asm_line, &pos);
        if (r1 < 0 || r2 < 0) return 0;

        emit_byte(code, &len, 0xF3);
        emit_byte(code, &len, 0x0F);
        emit_byte(code, &len, 0x5E);
        emit_byte(code, &len, 0xC0 | (r2 << 3) | r1);
        }

    else if (!strncmp(&asm_line[pos], "xorps ", 6)) {
            pos += 6;
            int r1 = parse_reg(asm_line, &pos);
            while (asm_line[pos] == ' ' || asm_line[pos] == ',') pos++;
            int r2 = parse_reg(asm_line, &pos);
            if (r1 < 0 || r2 < 0) return 0;

            emit_byte(code, &len, 0x0F);
            emit_byte(code, &len, 0x57);
            emit_byte(code, &len, 0xC0 | (r2 << 3) | r1);
            }

    else {
        print("Unknown instruction: ");
        print(asm_line);
        print("\n");
        return 0;
    }

    emit_byte(code, &len, 0xC3);
    uint32_t res = jit_call(code);
    print_hex(res);

    jit_offset += len;
    return 1;
}