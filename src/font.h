#ifndef ATMKOALA_FONT_H
#define ATMKOALA_FONT_H
/* font.h — atmkoala v0.5
 * Современный шрифт 8x16 с поддержкой ASCII + кириллицы UTF-8
 * Стиль: чистый, без засечек — похож на IBM VGA но сглаженнее
 */
#include <stdint.h>

#define FONT_W  8
#define FONT_H  16

/* Получить bitmap строки для символа (возвращает указатель на 16 байт) */
const uint8_t *font_get_glyph(uint32_t codepoint);

/* Декодировать UTF-8 последовательность, вернуть codepoint и продвинуть ptr */
uint32_t utf8_decode(const uint8_t **ptr);

/* Количество символов в UTF-8 строке */
int utf8_strlen(const char *s);

#endif /* ATMKOALA_FONT_H */
