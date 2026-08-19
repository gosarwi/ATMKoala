/* osbuilder.c — No-code OS Builder — atmkoala v0.5
 *
 * Позволяет создать свою ОС через простые вопросы.
 * Всё сохраняется в /uiu/etc/myos.conf и применяется сразу.
 */
#include "osbuilder.h"
#include "config.h"
#include "vga.h"
#include "vfs.h"
#include "util.h"
#include "keyboard.h"
#include <stdint.h>
#include <stddef.h>

/* extern readline из kernel.c */
extern int use_vbe;

/* Простой readline для osbuilder */
static void ob_readline(char *out, int maxlen, const char *prompt) {
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_write(prompt);
    terminal_set_color(VGA_WHITE, VGA_BLACK);
    int len = 0; out[0] = 0;
    while (1) {
        int k;
        do {
            while (!(inb(0x64) & 1)) __asm__ volatile("pause");
            k = keyboard_poll();
        } while (k == 0);
        if (k == '\n' || k == '\r') { out[len]=0; terminal_putchar('\n'); return; }
        if ((k=='\b'||k==127) && len>0) {
            len--; out[len]=0;
            terminal_putchar('\b'); terminal_putchar(' '); terminal_putchar('\b');
        } else if (k>=' '&&k<='~'&&len<maxlen-1) {
            out[len++]=(char)k; out[len]=0; terminal_putchar((char)k);
        }
    }
}

static void ob_choice(char *out, int maxlen, const char *prompt,
                       const char **options, int n) {
    terminal_set_color(VGA_YELLOW, VGA_BLACK);
    terminal_writeln(prompt);
    for (int i = 0; i < n; i++) {
        terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        char nb[4]; kitoa(i+1, nb, 10);
        terminal_write("  ["); terminal_write(nb); terminal_write("] ");
        terminal_set_color(VGA_WHITE, VGA_BLACK);
        terminal_writeln(options[i]);
    }
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_write("  Выбор (1-");
    char nb2[4]; kitoa(n, nb2, 10); terminal_write(nb2);
    terminal_write("): ");
    terminal_set_color(VGA_WHITE, VGA_BLACK);

    while (1) {
        int k;
        do {
            while (!(inb(0x64) & 1)) __asm__ volatile("pause");
            k = keyboard_poll();
        } while (k == 0);
        if (k >= '1' && k <= '0'+n) {
            terminal_putchar((char)k); terminal_putchar('\n');
            kstrcpy(out, options[k-'1']);
            return;
        }
    }
}

void osbuilder_run(void) {
    terminal_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
    terminal_writeln("                                              ");
    terminal_writeln("   atmkoala OS Builder v0.5     ");
    terminal_writeln("   Создай свою ОС без программирования!       ");
    terminal_writeln("                                              ");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    terminal_writeln("");

    char buf[128];
    cfg_file_t cfg; kmemset(&cfg, 0, sizeof(cfg));

    /* 1. Имя ОС */
    terminal_set_color(VGA_YELLOW, VGA_BLACK);
    terminal_writeln("═══ Шаг 1: Основные настройки ═══");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    ob_readline(buf, 64, "  Название вашей ОС [например: MyOS]: ");
    if (!buf[0]) kstrcpy(buf, "MyOS");
    cfg_set(&cfg, "os", "name", buf);

    ob_readline(buf, 32, "  Версия [1.0]: ");
    if (!buf[0]) kstrcpy(buf, "1.0");
    cfg_set(&cfg, "os", "version", buf);

    ob_readline(buf, 32, "  Кодовое имя [например: sunrise]: ");
    if (!buf[0]) kstrcpy(buf, "sunrise");
    cfg_set(&cfg, "os", "codename", buf);

    ob_readline(buf, 64, "  Автор/разработчик: ");
    if (!buf[0]) kstrcpy(buf, "Anonymous");
    cfg_set(&cfg, "os", "author", buf);

    /* 2. Внешний вид */
    terminal_set_color(VGA_YELLOW, VGA_BLACK);
    terminal_writeln("\n═══ Шаг 2: Внешний вид ═══");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    terminal_writeln("  Внешний вид: фиксированная светлая тема Paper.");
    cfg_set(&cfg,"desktop","style","paper");

    ob_readline(buf, 64, "  Текст на обоях рабочего стола [пусто=логотип]: ");
    if (buf[0]) cfg_set(&cfg,"desktop","wallpaper",buf);

    /* 3. Командная строка */
    terminal_set_color(VGA_YELLOW, VGA_BLACK);
    terminal_writeln("\n═══ Шаг 3: Командная строка ═══");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    ob_readline(buf, 64, "  Имя пользователя [root]: ");
    if (!buf[0]) kstrcpy(buf, "root");
    cfg_set(&cfg, "os", "username", buf);

    ob_readline(buf, 64, "  Hostname [myos]: ");
    if (!buf[0]) kstrcpy(buf, "myos");
    cfg_set(&cfg, "system", "hostname", buf);

    ob_readline(buf, 128, "  Формат промпта [%u@%h:%d%$ ]: ");
    if (buf[0]) cfg_set(&cfg, "kernel", "prompt_format", buf);

    ob_readline(buf, 128, "  Загрузочное сообщение: ");
    if (buf[0]) cfg_set(&cfg, "kernel", "boot_msg", buf);

    /* 4. Функции */
    terminal_set_color(VGA_YELLOW, VGA_BLACK);
    terminal_writeln("\n═══ Шаг 4: Функции ═══");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    const char *widget_opts[] = {
        "Нет", "Часы (только)", "Дата + время", "Системная информация"
    };
    ob_choice(buf, 32, "  Виджет на рабочем столе:", widget_opts, 4);
    if (kstrcmp(buf, "Нет") != 0) {
        if (kstrstr(buf,"Часы")) cfg_set(&cfg,"desktop","widget","clock");
        else if (kstrstr(buf,"Дата")) cfg_set(&cfg,"desktop","widget","datetime");
        else cfg_set(&cfg,"desktop","widget","sysinfo");
    }

    const char *net_opts[] = {"Включить", "Выключить"};
    ob_choice(buf, 16, "  Сеть:", net_opts, 2);
    cfg_set(&cfg, "net", "enabled",
            kstrcmp(buf, "Включить") == 0 ? "yes" : "no");

    /* 5. Приложения */
    terminal_set_color(VGA_YELLOW, VGA_BLACK);
    terminal_writeln("\n═══ Шаг 5: Приложения ═══");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    ob_readline(buf, 64, "  Web client name [disabled]: ");
    if (buf[0]) cfg_set(&cfg, "apps", "web_client_name", buf);

    ob_readline(buf, 128, "  Команды которые отключить (через запятую): ");
    if (buf[0]) cfg_set(&cfg, "kernel", "disable_cmd", buf);

    /* 6. Пароль */
    terminal_set_color(VGA_YELLOW, VGA_BLACK);
    terminal_writeln("\n═══ Шаг 6: Безопасность ═══");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    ob_readline(buf, 16, "  Пин-код sudo [1234]: ");
    if (!buf[0]) kstrcpy(buf, "1234");
    cfg_set(&cfg, "system", "sudo_pin", buf);

    /* Сохраняем */
    terminal_set_color(VGA_YELLOW, VGA_BLACK);
    terminal_writeln("\n═══ Сохранение ═══");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* Создаём VFS путь */
    vfs_mkdir("/uiu", 0755); vfs_mkdir("/uiu/etc", 0755);
    cfg_save(&cfg, "/uiu/etc/myos.conf");

    /* Применяем к системному конфигу */
    const char *v;
    v = cfg_get(&cfg,"os","name");       if(v) sysconf_set("kernel","kernel_name",v);
    v = cfg_get(&cfg,"os","theme");      if(v) sysconf_set("console","theme",v);
    v = cfg_get(&cfg,"desktop","style"); if(v) sysconf_set("desktop","style",v);
    v = cfg_get(&cfg,"desktop","bg_color"); if(v) sysconf_set("desktop","bg_color",v);
    v = cfg_get(&cfg,"desktop","wallpaper");if(v) sysconf_set("desktop","wallpaper",v);
    v = cfg_get(&cfg,"desktop","widget"); if(v) sysconf_set("desktop","widget",v);
    v = cfg_get(&cfg,"system","hostname");if(v) sysconf_set("system","hostname",v);
    v = cfg_get(&cfg,"kernel","prompt_format");if(v) sysconf_set("kernel","prompt_format",v);
    v = cfg_get(&cfg,"kernel","boot_msg");if(v) sysconf_set("kernel","boot_msg",v);
    v = cfg_get(&cfg,"kernel","disable_cmd");if(v) sysconf_set("kernel","disable_cmd",v);
    v = cfg_get(&cfg,"system","sudo_pin");if(v) sysconf_set("system","sudo_pin",v);
    v = cfg_get(&cfg,"apps","web_client_name");if(v) sysconf_set("apps","web_client_name",v);
    sysconf_save();

    terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    terminal_writeln("");
    terminal_writeln("  ✓ Конфиг сохранён в /uiu/etc/myos.conf");
    terminal_writeln("  ✓ Изменения применены к системе");
    terminal_writeln("  ✓ Перезапустите DE командой 'de' для применения стиля");
    terminal_writeln("");
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);

    /* Показываем итог */
    v = cfg_get(&cfg,"os","name");
    terminal_write("  Ваша ОС: "); terminal_write(v?v:"MyOS");
    v = cfg_get(&cfg,"os","version");
    terminal_write(" v"); terminal_writeln(v?v:"1.0");

    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void osbuilder_show(void) {
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_writeln("=== OS Builder — Текущий конфиг (/uiu/etc/myos.conf) ===");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    cfg_file_t cfg; kmemset(&cfg, 0, sizeof(cfg));
    if (cfg_load(&cfg, "/uiu/etc/myos.conf") < 0) {
        terminal_writeln("  Конфиг не найден. Запустите: osbuilder");
        return;
    }
    for (int s = 0; s < cfg.count; s++) {
        terminal_set_color(VGA_YELLOW, VGA_BLACK);
        terminal_write("  ["); terminal_write(cfg.sections[s].name); terminal_writeln("]");
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        for (int k = 0; k < cfg.sections[s].count; k++) {
            terminal_write("    "); terminal_write(cfg.sections[s].entries[k].key);
            terminal_write(" = "); terminal_writeln(cfg.sections[s].entries[k].val);
        }
    }
}

void osbuilder_apply(void) {
    cfg_file_t cfg; kmemset(&cfg, 0, sizeof(cfg));
    if (cfg_load(&cfg, "/uiu/etc/myos.conf") < 0) return;
    const char *v;
    v = cfg_get(&cfg,"os","name");      if(v) sysconf_set("kernel","kernel_name",v);
    v = cfg_get(&cfg,"os","theme");     if(v) sysconf_set("console","theme",v);
    v = cfg_get(&cfg,"desktop","style");if(v) sysconf_set("desktop","style",v);
    sysconf_save();
    terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    terminal_writeln("OS конфиг применён.");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}
