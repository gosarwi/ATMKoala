/*
 * bsd_compat.c — implementation of the bus_space(9)-shaped API
 * declared in bsd_compat.h. Original code, not derived from any
 * NetBSD source file — see the long comment in the header for why
 * this exists and what it deliberately does not copy.
 */
#include "bsd_compat.h"
#include "util.h"   /* inb/outb/inw/outw/inl/outl already defined here */

uint8_t bus_space_read_1(const bus_space_handle_t *h, uint64_t off) {
    if (h->type == BUS_SPACE_IO)
        return inb((uint16_t)(h->base + off));
    return *(volatile uint8_t *)(uintptr_t)(h->base + off);
}

uint16_t bus_space_read_2(const bus_space_handle_t *h, uint64_t off) {
    if (h->type == BUS_SPACE_IO)
        return inw((uint16_t)(h->base + off));
    return *(volatile uint16_t *)(uintptr_t)(h->base + off);
}

uint32_t bus_space_read_4(const bus_space_handle_t *h, uint64_t off) {
    if (h->type == BUS_SPACE_IO)
        return inl((uint16_t)(h->base + off));
    return *(volatile uint32_t *)(uintptr_t)(h->base + off);
}

void bus_space_write_1(const bus_space_handle_t *h, uint64_t off, uint8_t v) {
    if (h->type == BUS_SPACE_IO)
        outb((uint16_t)(h->base + off), v);
    else
        *(volatile uint8_t *)(uintptr_t)(h->base + off) = v;
}

void bus_space_write_2(const bus_space_handle_t *h, uint64_t off, uint16_t v) {
    if (h->type == BUS_SPACE_IO)
        outw((uint16_t)(h->base + off), v);
    else
        *(volatile uint16_t *)(uintptr_t)(h->base + off) = v;
}

void bus_space_write_4(const bus_space_handle_t *h, uint64_t off, uint32_t v) {
    if (h->type == BUS_SPACE_IO)
        outl((uint16_t)(h->base + off), v);
    else
        *(volatile uint32_t *)(uintptr_t)(h->base + off) = v;
}

void bus_space_read_region_1(const bus_space_handle_t *h, uint64_t off,
                              uint8_t *buf, size_t count) {
    for (size_t i = 0; i < count; i++)
        buf[i] = bus_space_read_1(h, off + i);
}

void bus_space_write_region_1(const bus_space_handle_t *h, uint64_t off,
                               const uint8_t *buf, size_t count) {
    for (size_t i = 0; i < count; i++)
        bus_space_write_1(h, off + i, buf[i]);
}
