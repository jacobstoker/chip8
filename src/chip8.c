#include <assert.h>
#include <raylib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILENAME            "../test/IBM Logo.ch8"

#define UNUSED(x)           (void)(x)
#define ARRAY_SIZE(arr)     (sizeof((arr)) / sizeof((arr)[0]))

#define NNN(op)             ((uint16_t)(op) & 0x0FFF)
#define N(op)               ((uint8_t)(op) & 0x000F)
#define X(op)               ((uint8_t)((op) >> 8) & 0x0F)
#define Y(op)               ((uint8_t)((op) >> 4) & 0x0F)
#define KK(op)              ((uint8_t)(op) & 0x00FF)

#define RAM_SIZE            0x1000 // 4KB of RAM
#define FONT_BASE_ADDR      0x050
#define PROGRAM_BASE_ADDR   0x200
#define PROGRAM_REGION_END  0xFFF
#define PROGRAM_REGION_SIZE (PROGRAM_REGION_END - PROGRAM_BASE_ADDR + 1)

#define WIDTH               64
#define HEIGHT              32
#define SCALE               10
#define CPU_STEPS_PER_FRAME 10
#define FPS_TARGET          60

#define FONT_BYTES          (16 * 5)

#pragma region System

// clang-format off
const uint8_t font_sprites[FONT_BYTES] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80, // F
};
// clang-format on

typedef struct {
    int qwerty_key;
    uint8_t chip8_key;
} KeyMapping;

// Convert between the weird chip8 keypad and a normal querty keyboard
// Raylib functions are using and returning keycodes
//
// | 1 | 2 | 3 | C |         | 1 | 2 | 3 | 4 |
// | 4 | 5 | 6 | D |    ->   | Q | W | E | R |
// | 7 | 8 | 9 | E |         | A | S | D | F |
// | A | 0 | B | F |         | Z | X | C | V |
//
// clang-format off
static const KeyMapping valid_keys[] = {
    {KEY_ONE, 0x1}, {KEY_TWO, 0x2}, {KEY_THREE, 0x3}, {KEY_FOUR, 0xC}, 
    {KEY_Q,   0x4}, {KEY_W,   0x5}, {KEY_E,     0x6}, {KEY_R,    0xD}, 
    {KEY_A,   0x7}, {KEY_S,   0x8}, {KEY_D,     0x9}, {KEY_F,    0xE},
    {KEY_Z,   0xA}, {KEY_X,   0x0}, {KEY_C,     0xB}, {KEY_V,    0xF}
};
// clang-format on

uint8_t get_key_pressed(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(valid_keys); i++) {
        KeyMapping key = valid_keys[i];
        if (IsKeyDown(key.qwerty_key)) {
            return key.chip8_key;
        }
    }
    return 0xFF;
}

bool is_key_pressed(uint8_t chip8_key)
{
    for (size_t i = 0; i < ARRAY_SIZE(valid_keys); i++) {
        KeyMapping key = valid_keys[i];
        if (chip8_key == key.chip8_key) {
            return IsKeyDown(key.qwerty_key);
        }
    }
    assert(false);
    return false;
}

uint8_t V[16];    // Registers Vx (V0-VF) (general purpose)
uint16_t I;       // Register I (generally used to store memory addresses)
uint16_t DT;      // Register DT (delay timer)
uint16_t ST;      // Register ST (sound timer)

#define VF V[0xF] // Alias VF just to make it look nicer

uint16_t stack[16];
uint16_t stack_ptr;
uint16_t PC;
uint8_t *RAM;

uint8_t framebuffer[32][64];
bool screen_needs_update = false;

#pragma endregion
#pragma region opcodes

// 0nnn - SYS addr
// Jump to a machine code routine at nnn.
// This opcode is only used on the old computers on which Chip-8 was originally implemented. It is ignored by modern interpreters.
void op_sys(uint16_t opcode)
{
    printf("Called SYS addr\n");
    UNUSED(opcode);
}

// 00E0 - CLS
// Clear the display.
void op_cls(uint16_t opcode)
{
    printf("Called CLS\n");
    UNUSED(opcode);
    ClearBackground(RAYWHITE);
}

// 00EE - RET
// Return from a subroutine.
// The interpreter sets the program counter to the address at the top of the stack, then subtracts 1 from the stack pointer.
void op_ret(uint16_t opcode)
{
    printf("Called RET\n");
    UNUSED(opcode);
    PC = stack[stack_ptr];
    stack_ptr--;
}

// 1nnn - JP addr
// Jump to location nnn.
// The interpreter sets the program counter to nnn.
void op_jp_addr(uint16_t opcode)
{
    printf("Called JP addr (%04x)\n", NNN(opcode));
    PC = NNN(opcode);
}

// 2nnn - CALL addr
// Call subroutine at nnn.
// The interpreter increments the stack pointer, then puts the current PC on the top of the stack. The PC is then set to nnn.
void op_call(uint16_t opcode)
{
    printf("Called CALL addr (%04x)\n", NNN(opcode));
    stack_ptr++;
    stack[stack_ptr] = PC;
    PC               = NNN(opcode);
}

// 3xkk - SE Vx, byte
// Skip next opcode if Vx = kk.
// The interpreter compares register Vx to kk, and if they are equal, increments the program counter by 2.
void op_se_vx_byte(uint16_t opcode)
{
    uint8_t x  = X(opcode);
    uint8_t kk = KK(opcode);
    printf("Called SE Vx, byte (V%d, %04x)\n", x, kk);

    if (V[x] == kk) {
        PC += 2;
    }
}

// 4xkk - SNE Vx, byte
// Skip next opcode if Vx != kk.
// The interpreter compares register Vx to kk, and if they are not equal, increments the program counter by 2.
void op_sne_vx_byte(uint16_t opcode)
{
    uint8_t x  = X(opcode);
    uint8_t kk = KK(opcode);
    printf("Called SNE Vx, byte (V%d, %04x)\n", x, kk);

    if (V[x] != kk) {
        PC += 2;
    }
}

// 5xy0 - SE Vx, Vy
// Skip next opcode if Vx = Vy.
// The interpreter compares register Vx to register Vy, and if they are equal, increments the program counter by 2.
void op_se_vx_vy(uint16_t opcode)
{
    uint8_t x = X(opcode);
    uint8_t y = Y(opcode);
    printf("Called SE Vx, Vy (V%d, V%d)\n", x, y);

    if (V[x] == V[y]) {
        PC += 2;
    }
}

// 6xkk - LD Vx, byte
// Set Vx = kk.
// The interpreter puts the value kk into register Vx.
void op_ld_vx_byte(uint16_t opcode)
{
    uint8_t x  = X(opcode);
    uint8_t kk = KK(opcode);
    printf("Called LD Vx, byte (V%d, %04x)\n", x, kk);

    V[x] = kk;
}

// 7xkk - ADD Vx, byte
// Set Vx = Vx + kk.
// Adds the value kk to the value of register Vx, then stores the result in Vx.
void op_add_vx_byte(uint16_t opcode)
{
    uint8_t x  = X(opcode);
    uint8_t kk = KK(opcode);
    printf("Called ADD (V%d, %04x)\n", x, kk);
    uint8_t tmp = V[x] + kk;
    printf("%d + %d = %d\n", V[x], kk, tmp);

    V[x] = tmp;
}

// 8xy0 - LD Vx, Vy
// Set Vx = Vy.
// Stores the value of register Vy in register Vx.
void op_ld_vx_vy(uint16_t opcode)
{
    uint8_t x = X(opcode);
    uint8_t y = Y(opcode);
    printf("Called LD Vx, Vy (V%d, V%d)\n", x, y);

    V[x] = V[y];
}

// 8xy1 - OR Vx, Vy
// Set Vx = Vx OR Vy.
// Performs a bitwise OR on the values of Vx and Vy, then stores the result in Vx. A bitwise OR compares the corrseponding bits from two values, and if either bit is 1, then the same bit in the result is also 1. Otherwise, it is 0.
void op_or(uint16_t opcode)
{
    uint8_t x = X(opcode);
    uint8_t y = Y(opcode);
    printf("Called OR Vx, Vy (V%d, V%d)\n", x, y);

    V[x] = V[x] | V[y];
}

// 8xy2 - AND Vx, Vy
// Set Vx = Vx AND Vy.
// Performs a bitwise AND on the values of Vx and Vy, then stores the result in Vx. A bitwise AND compares the corrseponding bits from two values, and if both bits are 1, then the same bit in the result is also 1. Otherwise, it is 0.
void op_and(uint16_t opcode)
{
    uint8_t x = X(opcode);
    uint8_t y = Y(opcode);
    printf("Called AND Vx, Vy (V%d, V%d)\n", x, y);

    V[x] = V[x] & V[y];
}

// 8xy3 - XOR Vx, Vy
// Set Vx = Vx XOR Vy.
// Performs a bitwise exclusive OR on the values of Vx and Vy, then stores the result in Vx. An exclusive OR compares the corrseponding bits from two values, and if the bits are not both the same, then the corresponding bit in the result is set to 1. Otherwise, it is 0.
void op_xor(uint16_t opcode)
{
    uint8_t x = X(opcode);
    uint8_t y = Y(opcode);
    printf("Called XOR Vx, Vy (V%d, V%d)\n", x, y);

    V[x] = V[x] ^ V[y];
}

// 8xy4 - ADD Vx, Vy
// Set Vx = Vx + Vy, set VF = carry.
// The values of Vx and Vy are added together. If the result is greater than 8 bits (i.e., > 255,) VF is set to 1, otherwise 0. Only the lowest 8 bits of the result are kept, and stored in Vx.
void op_add_vx_vy(uint16_t opcode)
{
    uint8_t x = X(opcode);
    uint8_t y = Y(opcode);
    printf("Called ADD Vx, Vy (V%d, V%d)\n", x, y);

    uint16_t tmp = V[x] + V[y];

    VF = (tmp > 255);
    printf("%d + %d = %d (%d)\n", V[x], V[y], tmp, V[0xF]);
    V[x] = (uint8_t)(tmp & 0xFF);
}

// 8xy5 - SUB Vx, Vy
// Set Vx = Vx - Vy, set VF = NOT borrow.
// If Vx > Vy, then VF is set to 1, otherwise 0. Then Vy is subtracted from Vx, and the results stored in Vx.
void op_sub(uint16_t opcode)
{
    uint8_t x = X(opcode);
    uint8_t y = Y(opcode);
    printf("Called SUB Vx, Vy (V%d, V%d)\n", x, y);

    VF           = (V[x] >= V[y]);
    uint16_t tmp = V[x] - V[y];
    printf("%d - %d = %d\n", V[x], V[y], tmp);
    V[x] = (uint8_t)tmp;
}

// 8xy6 - SHR Vx {, Vy}
// Set Vx = Vx SHR 1.
// If the least-significant bit of Vx is 1, then VF is set to 1, otherwise 0. Then Vx is divided by 2.
void op_shr(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called SHR Vx {, Vy} (V%d)\n", x);

    VF   = V[x] & 1;
    V[x] = V[x] >> 1;
}

// 8xy7 - SUBN Vx, Vy
// Set Vx = Vy - Vx, set VF = NOT borrow.
// If Vy > Vx, then VF is set to 1, otherwise 0. Then Vx is subtracted from Vy, and the results stored in Vx.
void op_subn(uint16_t opcode)
{
    uint8_t x = X(opcode);
    uint8_t y = Y(opcode);
    printf("Called SUBN Vx, Vy (V%d, V%d)\n", x, y);

    VF           = V[y] >= V[x];
    uint16_t tmp = V[y] - V[x];

    printf("%d - %d = %d\n", V[x], V[y], tmp);
    V[x] = (uint8_t)(tmp);
}

// 8xyE - SHL Vx {, Vy}
// Set Vx = Vx SHL 1.
// If the most-significant bit of Vx is 1, then VF is set to 1, otherwise to 0. Then Vx is multiplied by 2.
void op_shl(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called SHL Vx {, Vy} (V%d)\n", x);

    VF = (V[x] & (0x80)) >> 7;
    V[x] <<= 1;
}

// 9xy0 - SNE Vx, Vy
// Skip next opcode if Vx != Vy.
// The values of Vx and Vy are compared, and if they are not equal, the program counter is increased by 2.
void op_sne_vx_vy(uint16_t opcode)
{
    uint8_t x = X(opcode);
    uint8_t y = Y(opcode);
    printf("Called SNE Vx, Vy (V%d, V%d)\n", x, y);

    if (V[x] != V[y]) {
        PC += 2;
    }
}

// Annn - LD I, addr
// Set I = nnn.
// The value of register I is set to nnn.
void op_ld_i_addr(uint16_t opcode)
{
    printf("Called LD I, addr (%04x)\n", NNN(opcode));
    I = NNN(opcode);
}

// Bnnn - JP V0, addr
// Jump to location nnn + V0.
// The program counter is set to nnn plus the value of V0.
void op_jp_v0_addr(uint16_t opcode)
{
    printf("Called JP V0, addr (%04x)\n", NNN(opcode));
    PC = NNN(opcode) + V[0];
}

// Cxkk - RND Vx, byte
// Set Vx = random byte AND kk.
// The interpreter generates a random number from 0 to 255, which is then ANDed with the value kk.
// The results are stored in Vx. See opcode 8xy2 for more information on AND.
void op_rnd(uint16_t opcode)
{
    uint8_t x  = X(opcode);
    uint8_t kk = KK(opcode);
    printf("Called RND Vx, byte (V%d, %04x)\n", x, kk);
    uint8_t rnd = (uint8_t)(rand() & 0xFF);
    V[x]        = kk & rnd;
}

// Dxyn - DRW Vx, Vy, nibble
// Display n-byte sprite starting at memory location I at (Vx, Vy), set VF = collision.
// The interpreter reads n bytes from memory, starting at the address stored in I.
// These bytes are then displayed as sprites on screen at coordinates (Vx, Vy). Sprites are XORed onto the existing screen.
// If this causes any pixels to be erased, VF is set to 1, otherwise it is set to 0.
// If the sprite is positioned so part of it is outside the coordinates of the display, it wraps around to the opposite side of the screen.
// See opcode 8xy3 for more information on XOR, and section 2.4, Display, for more information on the Chip-8 screen and sprites.
void op_drw(uint16_t opcode)
{
    VF = 0;

    uint8_t vx = V[X(opcode)];
    uint8_t vy = V[Y(opcode)];
    uint8_t n  = N(opcode);
    printf("Called DRW Vx, Vy, nibble (V%d, V%d, %04x)\n", X(opcode), Y(opcode), n);

    for (uint8_t row = 0; row < n; row++) {
        uint8_t byte = RAM[I + row];

        for (uint8_t col = 0; col < 8; col++) {
            uint8_t sprite_bit = (byte >> (7 - col)) & 0x01;

            if (sprite_bit == 0) {
                continue;
            }

            uint8_t px = (vx + col) % WIDTH;
            uint8_t py = (vy + row) % HEIGHT;
            if (framebuffer[py][px] == 1) {
                VF = 1;
            }
            framebuffer[py][px] ^= 1;
        }
    }

    screen_needs_update = true;
}

// Ex9E - SKP Vx
// Skip next opcode if key with the value of Vx is pressed.
// Checks the keyboard, and if the key corresponding to the value of Vx is currently in the down position, PC is increased by 2.
void op_skp(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called SKP Vx (V%d)\n", x);

    if (is_key_pressed(V[x])) {
        PC += 2;
    }
}

// ExA1 - SKNP Vx
// Skip next opcode if key with the value of Vx is not pressed.
// Checks the keyboard, and if the key corresponding to the value of Vx is currently in the up position, PC is increased by 2.
void op_sknp(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called SKNP Vx (V%d)\n", x);

    if (!is_key_pressed(V[x])) {
        PC += 2;
    }
}

// Fx07 - LD Vx, DT
// Set Vx = delay timer value.
// The value of DT is placed into Vx.
void op_ld_vx_dt(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called LD Vx, DT (V%d)\n", x);
    V[x] = (uint8_t)DT;
}

// Fx0A - LD Vx, K
// Wait for a key press, store the value of the key in Vx.
// All execution stops until a key is pressed, then the value of that key is stored in Vx.
void op_ld_vx_k(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called LD Vx, K (V%d)\n", x);

    uint8_t key = 0xFF;

    while (key == 0xFF) {
        key = get_key_pressed();
        // TODO add delay here?
    }

    V[x] = key;
}

// Fx15 - LD DT, Vx
// Set delay timer = Vx.
// DT is set equal to the value of Vx.
void op_ld_dt_vx(uint16_t opcode)
{
    printf("Called LD DT, Vx\n");
    uint8_t x = X(opcode);
    DT        = V[x];
}

// Fx18 - LD ST, Vx
// Set sound timer = Vx.
// ST is set equal to the value of Vx.
void op_ld_st_vx(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called LD ST, Vx (V%d)\n", x);
    ST = V[x];
}

// Fx1E - ADD I, Vx
// Set I = I + Vx.
// The values of I and Vx are added, and the results are stored in I.
void op_add_i_vx(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called ADD I, Vx (V%d)\n", x);
    I = V[x] + I;
}

// Fx29 - LD F, Vx
// Set I = location of sprite for digit Vx.
// The value of I is set to the location for the hexadecimal sprite corresponding to the value of Vx. See section 2.4, Display, for more information on the Chip-8 hexadecimal font.
void op_ld_f_vx(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called LD F, Vx (V%d)\n", x);
    I = (uint16_t)(FONT_BASE_ADDR + (V[x] * 5));
}

// Fx33 - LD B, Vx
// Store BCD representation of Vx in memory locations I, I+1, and I+2.
// The interpreter takes the decimal value of Vx, and places the hundreds digit in memory at location in I,
// the tens digit at location I+1, and the ones digit at location I+2.
void op_ld_b_vx(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called LD B, Vx(V%d)\n", x);

    RAM[I]     = (V[x] / 100);     // Hundreds
    RAM[I + 1] = (V[x] / 10) % 10; // Tens
    RAM[I + 2] = (V[x] % 10);      // Ones
}

// Fx55 - LD [I], Vx
// Store registers V0 through Vx in memory starting at location I.
// The interpreter copies the values of registers V0 through Vx into memory, starting at the address in I.
void op_ld_i_vx(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called LD [I], Vx (V%d)\n", x);
    memcpy(RAM + I, V, x + 1);
}

// Fx65 - LD Vx, [I]
// Read registers V0 through Vx from memory starting at location I.
// The interpreter reads values from memory starting at location I into registers V0 through Vx.
void op_ld_vx_i(uint16_t opcode)
{
    uint8_t x = X(opcode);
    printf("Called LD Vx, [I] (V%d)\n", x);

    for (uint8_t idx = 0; idx <= x; idx++) {
        printf("Writing %d to V%d\n", RAM[I + idx], idx);
        V[idx] = RAM[I + idx];
    }
}

#pragma endregion
#pragma region Handling

typedef void (*OpcodeFunc)(uint16_t);
OpcodeFunc main_table[16];

void op_0xxx_handler(uint16_t opcode)
{
    switch (opcode) {
    case 0x00E0:
        op_cls(opcode);
        break;
    case 0x00EE:
        op_ret(opcode);
        break;
    default:
        op_sys(opcode);
        break;
    }
}

void op_8xxx_handler(uint16_t opcode)
{
    switch (opcode & 0x000F) {
    case 0x0:
        op_ld_vx_vy(opcode);
        break;
    case 0x1:
        op_or(opcode);
        break;
    case 0x2:
        op_and(opcode);
        break;
    case 0x3:
        op_xor(opcode);
        break;
    case 0x4:
        op_add_vx_vy(opcode);
        break;
    case 0x5:
        op_sub(opcode);
        break;
    case 0x6:
        op_shr(opcode);
        break;
    case 0x7:
        op_subn(opcode);
        break;
    case 0xE:
        op_shl(opcode);
        break;
    default:
        printf("Unknown opcode 0x%04x\n", opcode);
        assert(false);
    }
}

void op_Exxx_handler(uint16_t opcode)
{
    switch (opcode & 0x00FF) {
    case 0x9E:
        op_skp(opcode);
        break;
    case 0xA1:
        op_sknp(opcode);
        break;
    default:
        assert(false);
    }
}

void op_Fxxx_handler(uint16_t opcode)
{
    switch (opcode & 0x00FF) {
    case 0x07:
        op_ld_vx_dt(opcode);
        break;

    case 0x0A:
        op_ld_vx_k(opcode);
        break;

    case 0x15:
        op_ld_dt_vx(opcode);
        break;

    case 0x18:
        op_ld_st_vx(opcode);
        break;

    case 0x1E:
        op_add_i_vx(opcode);
        break;

    case 0x29:
        op_ld_f_vx(opcode);
        break;

    case 0x33:
        op_ld_b_vx(opcode);
        ;
        break;

    case 0x55:
        op_ld_i_vx(opcode);
        break;

    case 0x65:
        op_ld_vx_i(opcode);
        break;

    default:
        assert(false);
    }
}

OpcodeFunc main_table[16] = {
    op_0xxx_handler, // 0x0xxx
    op_jp_addr,      // 0x1xxx
    op_call,         // 0x2xxx
    op_se_vx_byte,   // 0x3xxx
    op_sne_vx_byte,  // 0x4xxx
    op_se_vx_vy,     // 0x5xxx
    op_ld_vx_byte,   // 0x6xxx
    op_add_vx_byte,  // 0x7xxx
    op_8xxx_handler, // 0x8xxx
    op_sne_vx_vy,    // 0x9xxx
    op_ld_i_addr,    // 0xAxxx
    op_jp_v0_addr,   // 0xBxxx
    op_rnd,          // 0xCxxx
    op_drw,          // 0xDxxx
    op_Exxx_handler, // 0xExxx
    op_Fxxx_handler, // 0xFxxx
};

void handle_opcode(uint16_t opcode)
{
    uint8_t category = ((opcode) >> 12) & 0x0F;

    main_table[category](opcode);
}

#pragma endregion
#pragma region Drawing

void draw_pixel(uint16_t x, uint16_t y, bool enable)
{
    assert(x <= WIDTH);
    assert(y <= HEIGHT);

    Color color = enable ? BLACK : WHITE;
    DrawRectangle(x * SCALE, y * SCALE, SCALE, SCALE, color);
}

void draw_screen(void)
{
    for (uint16_t x = 0; x < WIDTH; x++) {
        for (uint16_t y = 0; y < HEIGHT; y++) {
            draw_pixel(x, y, framebuffer[y][x]);
        }
    }
}

int copy_program_into_RAM(void)
{
    FILE *f = fopen(FILENAME, "rb");
    if (!f) {
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    if (file_size < 0 || (size_t)file_size > PROGRAM_REGION_SIZE) {
        fclose(f);
        return -1;
    }

    size_t bytes_read = fread(RAM + PROGRAM_BASE_ADDR, 1, (size_t)file_size, f);

    fclose(f);

    return (bytes_read == (size_t)file_size) ? 0 : -1;
}

int main(void)
{
    InitWindow(WIDTH * SCALE, HEIGHT * SCALE, "chip-8");
    SetTargetFPS(FPS_TARGET);

    RAM = calloc(RAM_SIZE, 1);
    memcpy(RAM + FONT_BASE_ADDR, font_sprites, (size_t)FONT_BYTES);

    int result = copy_program_into_RAM();
    if (result != 0) {
        printf("ERROR: Failed to copy program into RAM\n");
        free(RAM);
        return -1;
    }

    printf("NOTE: Copied program into RAM\n");

    PC              = PROGRAM_BASE_ADDR;
    uint16_t opcode = 0;

    while (!WindowShouldClose()) {
        if (DT > 0) {
            DT--;
        }

        if (ST > 0) {
            ST--;
        }

        for (int step = 0; step < CPU_STEPS_PER_FRAME; step++) {
            if (PC + 1 >= PROGRAM_REGION_END) {
                printf("ERROR: Program counter 0x%04x above region 0x%04x\n", PC + 1, PROGRAM_REGION_END);
                assert(false);
            }

            opcode = (uint16_t)((RAM[PC] << 8U) | RAM[PC + 1]);
            printf("Opcode 0x%04x, PC 0x%04x\n", opcode, PC);
            PC += 2;
            handle_opcode(opcode);
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        draw_screen();
        EndDrawing();
    }

    free(RAM);
    CloseWindow();
    return 0;
}

#pragma endregion
