#include <lexer.h>
#include <defs.h>

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define ARRLEN(arr) (sizeof(arr) / sizeof(arr[0]))

static FILE *s_file = NULL;
static struct token **s_toks = NULL;
static size_t s_tokcnt = 0;

const char *insts[] =
{
    [INST_ADD] = "add",
    [INST_OR]  = "or",
    [INST_ADC] = "adc",
    [INST_SBB] = "sbb",
    [INST_AND] = "and",
    [INST_SUB] = "sub",
    [INST_XOR] = "xor",
    [INST_CMP] = "cmp",
    [INST_MOV] = "mov",
    [INST_PUSH] = "push",
    [INST_POP] = "pop",
};

const char *regstrs[] =
{
    [LREG_AH] = "ah",
    [LREG_BH] = "bh",
    [LREG_CH] = "ch",
    [LREG_DH] = "dh",

    [LREG_AL] = "al",
    [LREG_BL] = "bl",
    [LREG_CL] = "cl",
    [LREG_DL] = "dl",
    [LREG_SPL] = "spl",
    [LREG_BPL] = "bpl",
    [LREG_SIL] = "sil",
    [LREG_DIL] = "dil",

    [LREG_AX] = "ax",
    [LREG_BX] = "bx",
    [LREG_CX] = "cx",
    [LREG_DX] = "dx",
    [LREG_SP] = "sp",
    [LREG_BP] = "bp",
    [LREG_SI] = "si",
    [LREG_DI] = "di",

    [LREG_EAX] = "eax",
    [LREG_EBX] = "ebx",
    [LREG_ECX] = "ecx",
    [LREG_EDX] = "edx",
    [LREG_ESP] = "esp",
    [LREG_EBP] = "ebp",
    [LREG_ESI] = "esi",
    [LREG_EDI] = "edi",

    [LREG_RAX] = "rax",
    [LREG_RBX] = "rbx",
    [LREG_RCX] = "rcx",
    [LREG_RDX] = "rdx",
    [LREG_RSP] = "rsp",
    [LREG_RBP] = "rbp",
    [LREG_RSI] = "rsi",
    [LREG_RDI] = "rdi"
};

static const char *keywords[] =
{
    [T_U8]  = "u8",
    [T_U16] = "u16",
    [T_U32] = "u32",
    [T_U64] = "u64"
};

#define FIND(sval, arr) (find(sval, arr, ARRLEN(arr)))

static int find(const char *sval, const char **arr, size_t len)
{
    for (unsigned int i = 0; i < len; i++)
        if (arr[i] && !strcmp(arr[i], sval)) return i;
    return -1;
}

void pushtok(int type, char *sval, unsigned long ival)
{
    *s_toks = realloc(*s_toks, (s_tokcnt + 1) * sizeof(struct token));
    (*s_toks)[s_tokcnt++] = (struct token) { .type = type, .sval = sval, .ival = ival };
}

int lexfile(FILE *file, struct token **toks)
{
    s_file = file;
    s_toks = toks;

    char c;
    while ((c = fgetc(file)) != EOF)
    {
        if (isspace(c)) continue;
        switch (c)
        {
            case ':': pushtok(T_COLON,  NULL, 0); continue;
            case ',': pushtok(T_COMMA,  NULL, 0); continue;
            case '[': pushtok(T_LBRACK, NULL, 0); continue;
            case ']': pushtok(T_RBRACK, NULL, 0); continue;
            case '+': pushtok(T_PLUS,   NULL, 0); continue;
            case '*': pushtok(T_STAR,   NULL, 0); continue;
            case '(': pushtok(T_LPAREN, NULL, 0); continue;
            case ')': pushtok(T_RPAREN, NULL, 0); continue;
                      
        }

        char str[32];
        int strl = 0;
        if (isalnum(c) || c == '_')
        {
            do str[strl++] = c;
            while ((c = fgetc(file)) != EOF && isalnum(c));
            
            str[strl] = 0;
            ungetc(c, file);

            if (isdigit(str[0]))
            {
                char *strp = str;
                int base = 10;

                if (str[0] == '0')
                {
                    if (str[1] == 'x')
                    {
                        strp += 2;
                        base = 16;
                    }
                    else if (strl > 1)
                    {
                        strp++;
                        base = 8;
                    }
                }
                unsigned long v = strtoull(str, NULL, base);
                pushtok(T_IMM, NULL, v);
            }
            else
            {
                int t;
                if ((t = FIND(str, insts)) != -1) pushtok(T_INST, NULL, t);
                else if ((t = FIND(str, regstrs)) != -1) pushtok(T_REG, NULL, t);
                else if ((t = FIND(str, keywords)) != -1) pushtok(t, NULL, 0);
                else pushtok(T_LBL, strdup(str), 0);
            }
        }
    }

    pushtok(T_EOF, NULL, 0);
    return 0;
}
