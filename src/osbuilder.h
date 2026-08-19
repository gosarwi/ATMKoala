#ifndef OSBUILDER_H
#define OSBUILDER_H
/* osbuilder.h — No-code OS Builder для atmkoala v0.5
 * Позволяет создавать свою ОС через конфиг без программирования
 */
#include <stdint.h>
#include <stddef.h>

void osbuilder_run(void);      /* Интерактивный мастер */
void osbuilder_apply(void);    /* Применить конфиг */
void osbuilder_show(void);     /* Показать текущую конфигурацию */

#endif
