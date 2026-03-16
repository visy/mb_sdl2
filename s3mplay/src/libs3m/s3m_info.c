#include <stdio.h>
#include <string.h>
#include "s3m.h"
#include "s3m_intern.h"

static uint16_t read16(const uint8_t* p, const uint8_t* end) {
    if (!p || p + 2 > end) return 0;
    return p[0] | (p[1] << 8);
}

void s3m_print_header(s3m_t* s3m)
{
    if (!s3m || !s3m->header) return;
    s3m_header_t* h = s3m->header;
    const uint8_t* end = s3m->buffer + s3m->filesize;
    printf("Song: %s\n", h->song_name);
    printf("Arrangement length: %d\n", read16((uint8_t*)&h->ordnum, end));
    printf("Instruments: %d\n", read16((uint8_t*)&h->insnum, end));
    printf("Patterns: %d\n", read16((uint8_t*)&h->patnum, end));
}

void s3m_print_channels(s3m_t* s3m)
{
    (void)s3m;
}

void s3m_print_arrangement(s3m_t* s3m)
{
    if (!s3m || !s3m->header || !s3m->order) return;
    const uint8_t* end = s3m->buffer + s3m->filesize;
    uint16_t len = read16((uint8_t*)&s3m->header->ordnum, end);
    printf("Arrangement: ");
    for (int i=0; i<len; i++) printf("%02X ", s3m->order[i]);
    printf("\n");
}

void s3m_print_instruments(s3m_t* s3m)
{
    if (!s3m || !s3m->header) return;
    const uint8_t* end = s3m->buffer + s3m->filesize;
    uint16_t num = read16((uint8_t*)&s3m->header->insnum, end);
    for (int i=0; i<num; i++) {
        if (s3m->instrument[i]) {
            printf("Inst %3d: %s\n", i+1, s3m->instrument[i]->sample.sampname);
        }
    }
}

void s3m_print_patterns(s3m_t* s3m)
{
    if (!s3m || !s3m->header) return;
    const uint8_t* end = s3m->buffer + s3m->filesize;
    uint16_t num = read16((uint8_t*)&s3m->header->patnum, end);
    for (int i=0; i<num; i++) {
        if (s3m->pattern[i]) {
            uint16_t len = read16(s3m->pattern[i], end);
            printf("Pattern %3d: %d bytes\n", i, len);
        }
    }
}
