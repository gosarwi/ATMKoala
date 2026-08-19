/* keyboard.c — atmkoala OS v0.5
 *
 * Поддержка раскладок:
 *   EN (по умолчанию) — стандартная US QWERTY
 *   RU — русская раскладка (ЙЦУКЕН), переключение Alt+Shift
 *
 * Русские символы возвращаются как KEY_UTF8,
 * UTF-8 байты помещаются в keyboard_utf8_buf[].
 *
 * Исправления v9:
 *   - Правильная обработка caps lock для кириллицы
 *   - Alt+Shift переключает раскладку без пропуска символов
 *   - Blocking getkey ждёт реального символа, не 0
 */
#include "keyboard.h"
#include "util.h"
#include <stdint.h>

#define KBD_DATA   0x60
#define KBD_STATUS 0x64

char keyboard_utf8_buf[4];

/* ── Английская раскладка (US QWERTY) ────────────────────── */
static const char en_lo[128] = {
/*00*/  0,   27,  '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
/*0F*/ '\t', 'q','w','e','r','t','y','u','i','o','p','[',']', '\n',
/*1D*/  0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
/*2A*/  0,   '\\','z','x','c','v','b','n','m',',','.','/',  0,
/*37*/ '*',   0,  ' ', 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/*44*/  0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0
};
static const char en_hi[128] = {
/*00*/  0,   27,  '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
/*0F*/ '\t', 'Q','W','E','R','T','Y','U','I','O','P','{','}', '\n',
/*1D*/  0,   'A','S','D','F','G','H','J','K','L',':','"', '~',
/*2A*/  0,   '|','Z','X','C','V','B','N','M','<','>','?',  0,
/*37*/ '*',   0,  ' ', 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/*44*/  0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0
};

/*
 * ── Русская раскладка ЙЦУКЕН ─────────────────────────────
 *
 * Каждый элемент — Unicode codepoint кириллического символа.
 * 0 = нет символа на этой позиции.
 *
 * Раскладка соответствует стандартной русской клавиатуре:
 *   й=q  ц=w  у=e  к=r  е=t  н=y  г=u  ш=i  щ=o  з=p  х=[  ъ=]
 *   ф=a  ы=s  в=d  а=f  п=g  р=h  о=j  л=k  д=l  ж=;  э='
 *   я=z  ч=x  с=c  м=v  и=b  т=n  ь=m  б=,  ю=.
 */
static const uint16_t ru_lo[128] = {
/* 00 */ 0,0, 0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30, 0x2D,0x3D, 0,
/* 0F */ 0,
  0x0439,0x0446,0x0443,0x043A,0x0435,0x043D,0x0433,0x0448,0x0449,0x0437,0x0445,0x044A, 0,
/* 1D */ 0,
  0x0444,0x044B,0x0432,0x0430,0x043F,0x0440,0x043E,0x043B,0x0434,0x0436,0x044D,
  0x0451, /* ` -> ё */
/* 2A */ 0,
  0x005C, /* \ */
  0x044F,0x0447,0x0441,0x043C,0x0438,0x0442,0x044C,0x0431,0x044E,
  0x002E, /* . */
  0,
/* 37 */ 0x002A, 0, 0x20, 0,0,0,0,0,0,0,0,0,0,
/* 44 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
/* Shift+русские = заглавные (codepoint строчная - 0x20 для кириллицы = +0x20 от 0x0430) */
static const uint16_t ru_hi[128] = {
/* 00 */ 0,0, 0x21,0x22,0x2116,0x3B,0x25,0x3A,0x3F,0x2A,0x28,0x29, 0x5F,0x2B, 0,
/* 0F */ 0,
  0x0419,0x0426,0x0423,0x041A,0x0415,0x041D,0x0413,0x0428,0x0429,0x0417,0x0425,0x042A, 0,
/* 1D */ 0,
  0x0424,0x042B,0x0412,0x0410,0x041F,0x0420,0x041E,0x041B,0x0414,0x0416,0x042D,
  0x0401, /* Shift+` -> Ё */
/* 2A */ 0,
  0x002F, /* / */
  0x042F,0x0427,0x0421,0x041C,0x0418,0x0422,0x042C,0x0411,0x042E,
  0x002C, /* , */
  0,
/* 37 */ 0,0,0x20,0,0,0,0,0,0,0,0,0,0,
/* 44 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/* ── Модификаторы ─────────────────────────────────────────── */
static int shift = 0, ctrl = 0, alt = 0, caps = 0, e0 = 0;
static int ru_layout = 0;   /* 0=EN, 1=RU */
static int alt_was   = 0;   /* для определения Alt+Shift */

/* ── UTF-8 кодирование codepoint ─────────────────────────── */
static int encode_utf8(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp; return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
}

/* ── 8042 PS/2 controller ports/commands ─────────────────── */
#define KBD_CMD_PORT     0x64   /* same physical port as KBD_STATUS, write side */
#define KBD_CMD_SELFTEST 0xAA   /* controller self-test */
#define KBD_CMD_ENABLE   0xAE   /* enable keyboard port (P2 / clock line) */
#define KBD_SELFTEST_OK  0x55
#define KBD_STATUS_AUX   0x20   /* output byte belongs to PS/2 mouse */

/* outb() already declared in util.h */

/* Wait (bounded) for the controller's output buffer to be empty, i.e.
 * safe to write a command. Real PS/2 controllers — especially ones
 * emulated on top of USB HID legacy support, common on modern OEM
 * laptops — can take a moment to settle right after boot. The old
 * code never waited for this at all and just assumed readiness,
 * which is fine under QEMU's instant-response virtual 8042 but not
 * guaranteed on real silicon. */
static int kbd_wait_input_clear(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(KBD_STATUS) & 0x02)) return 1; /* input buffer empty */
        __asm__ volatile("pause");
    }
    return 0; /* timed out — controller not responding */
}

static int kbd_wait_output_full(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(KBD_STATUS) & 0x01) return 1;    /* output buffer has data */
        __asm__ volatile("pause");
    }
    return 0;
}

void keyboard_init(void) {
    shift = ctrl = alt = caps = e0 = ru_layout = alt_was = 0;

    /* Drain any stale byte left in the output buffer, bounded so a
     * dead/missing controller can't hang boot forever. */
    for (int i = 0; i < 256 && (inb(KBD_STATUS) & 0x01); i++)
        inb(KBD_DATA);

    /* Ask the controller to self-test. If it never answers, the port
     * is genuinely absent or stuck — we skip the rest rather than
     * spin forever, and the rest of the OS still boots (just without
     * a keyboard) instead of hanging on a black/frozen screen. */
    if (!kbd_wait_input_clear()) return;
    outb(KBD_CMD_PORT, KBD_CMD_SELFTEST);
    if (kbd_wait_output_full()) {
        uint8_t result = inb(KBD_DATA);
        (void)result; /* expect KBD_SELFTEST_OK; proceed regardless —
                        * some firmwares don't implement this faithfully
                        * but the port still works for scancodes. */
    }

    /* Explicitly enable the keyboard clock/port. On some OEM EC/8042
     * implementations the port can come up disabled after a non-BIOS
     * boot path (e.g. a UEFI->CSM transition or a bootloader that
     * doesn't touch the controller at all, like Limine/GRUB handing
     * off without reinitializing it). */
    if (kbd_wait_input_clear())
        outb(KBD_CMD_PORT, KBD_CMD_ENABLE);

    /* Final drain in case the self-test/enable sequence produced
     * leftover bytes we don't care about. */
    for (int i = 0; i < 16 && (inb(KBD_STATUS) & 0x01); i++)
        inb(KBD_DATA);
}

/* Возвращает ASCII / KEY_* / KEY_UTF8 / 0 */
int keyboard_poll(void) {
    uint8_t status=inb(KBD_STATUS);
    if (!(status & 1)) return 0;
    /* Leave AUX bytes for IRQ12.  Consuming them here turns mouse packet
     * bytes into keyboard scancodes and corrupts focused text controls. */
    if (status & KBD_STATUS_AUX) return 0;

    uint8_t sc = inb(KBD_DATA);
    if (sc == 0xE0) { e0 = 1; return 0; }

    int ext     = e0; e0 = 0;
    int release = (sc & 0x80) != 0;
    uint8_t key = sc & 0x7F;

    /* ── Extended ── */
    if (ext) {
        if (key == 0x1D) { ctrl = !release; return 0; }
        if (key == 0x38) {
            if (!release) alt_was = 1;
            alt = !release;
            return 0;
        }
        if (release) return 0;
        switch (key) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x49: return KEY_PGUP;
            case 0x51: return KEY_PGDN;
            case 0x47: return KEY_HOME;
            case 0x4F: return KEY_END;
            case 0x53: return KEY_DEL;
            case 0x52: return KEY_INS;
            case 0x1C: return '\n';
        }
        return 0;
    }

    /* ── Модификаторы ── */
    if (key == 0x2A || key == 0x36) {
        int prev_shift = shift;
        shift = !release;
        /* Alt+Shift (или Shift+Alt) → переключить раскладку */
        if (!release && !prev_shift && alt) {
            ru_layout = !ru_layout;
            return 0;
        }
        if (!release && alt_was) {
            ru_layout = !ru_layout;
            alt_was = 0;
            return 0;
        }
        return 0;
    }
    if (key == 0x1D) { ctrl = !release; return 0; }
    if (key == 0x38) {
        if (!release) alt_was = shift ? 1 : 0;
        alt = !release;
        /* Если отпускаем Alt после Alt+Shift — переключаем */
        if (release && alt_was && shift) {
            ru_layout = !ru_layout;
            alt_was = 0;
        }
        return 0;
    }
    if (key == 0x3A && !release) { caps = !caps; return 0; }

    if (release) return 0;
    if (key >= 128) return 0;

    /* F-keys */
    if (key >= 0x3B && key <= 0x44) return KEY_F1 + (key - 0x3B);
    if (key == 0x57) return KEY_F11;
    if (key == 0x58) return KEY_F12;

    /* ── Русская раскладка ── */
    if (ru_layout) {
        int use_shift = shift;
        /* Caps lock инвертирует shift для букв */
        uint16_t cp_lo = ru_lo[key];
        if (caps && cp_lo >= 0x0430 && cp_lo <= 0x044F) use_shift = !use_shift;
        uint16_t cp = (uint16_t)(use_shift ? ru_hi[key] : ru_lo[key]);
        if (!cp) {
            /* Нет русского символа — пробуем английский (цифры, пунктуация) */
            goto english;
        }
        if (ctrl) {
            /* Ctrl+символ = Ctrl+соответствующая латинская буква */
            /* Для простоты — игнорируем Ctrl в русской раскладке */
        }
        /* Кодируем в UTF-8 */
        int n = encode_utf8((uint32_t)cp, keyboard_utf8_buf);
        keyboard_utf8_buf[n] = 0;
        return KEY_UTF8;
    }

english:;
    /* ── Английская раскладка ── */
    int use_shift = shift;
    char base = en_lo[key];
    if (caps && base >= 'a' && base <= 'z') use_shift = !use_shift;
    char c = use_shift ? en_hi[key] : en_lo[key];
    if (!c) return 0;

    if (ctrl) {
        if (c >= 'a' && c <= 'z') return c - 'a' + 1;
        if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
        if (c == '[') return 0x1B;
    }
    return (int)(unsigned char)c;
}

int keyboard_getkey(void) {
    int k;
    do {
        while (!(inb(KBD_STATUS) & 1)) __asm__ volatile("pause");
        k = keyboard_poll();
    } while (k == 0);
    return k;
}

int keyboard_shift(void)  { return shift; }
int keyboard_ctrl(void)   { return ctrl; }
int keyboard_alt(void)    { return alt; }
int keyboard_caps(void)   { return caps; }
int keyboard_ru(void)     { return ru_layout; }
