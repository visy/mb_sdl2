#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "channel.h"
#include "s3m.h"
#include "s3m_intern.h"

#define PI 3.14159265359

s3m_t* s3m__current_playing = NULL;

static uint16_t read16(const uint8_t* p, const uint8_t* end) {
    if (!p || p + 2 > end) return 0;
    return p[0] | (p[1] << 8);
}

static uint32_t read32(const uint8_t* p, const uint8_t* end) {
    if (!p || p + 4 > end) return 0;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void load_instruments(s3m_t* s3m, uint8_t* buffer, size_t length)
{
    const uint8_t* end = buffer + length;
    uint16_t num_instr = read16((uint8_t*)&s3m->header->insnum, end);
    if (num_instr > S3M_MAX_INSTRUMENTS) num_instr = S3M_MAX_INSTRUMENTS;
    
    uint16_t arrangement_len = read16((uint8_t*)&s3m->header->ordnum, end);
    uint32_t offs = 0x60 + arrangement_len;

    for (int i=0; i<num_instr; i++) {
        if (offs + 2 > length) break;
        uint32_t instr_offs = (uint32_t)read16(&buffer[offs], end) << 4;
        
        if (instr_offs == 0 || instr_offs > length - 80) {
            s3m->instrument[i] = NULL;
        } else {
            s3m->instrument[i] = (s3m_instrument_t*)&buffer[instr_offs];
            if (s3m->instrument[i]->type == 1) {
                uint32_t c2spd = read32((uint8_t*)&s3m->instrument[i]->sample.c2spd, end);
                if (c2spd < 1) c2spd = 8363;
                s3m->instr_c4_incr[i] = (double)c2spd / (double)s3m->samplerate;
            }
        }
        offs += 2;
    }
}

static void load_samples(s3m_t* s3m, uint8_t* buffer, size_t length)
{
    const uint8_t* end = buffer + length;
    uint16_t num_instr = read16((uint8_t*)&s3m->header->insnum, end);
    if (num_instr > S3M_MAX_INSTRUMENTS) num_instr = S3M_MAX_INSTRUMENTS;

    for (int i=0; i<num_instr; i++) {
        if (s3m->instrument[i] != NULL && s3m->instrument[i]->type == 1) {
            // S3M ParaPointer: HI byte at 0x0D, LO word at 0x0E (H-L-M order)
            uint32_t p_hi = s3m->instrument[i]->sample.memsegh;
            uint32_t p_lo = read16((uint8_t*)&s3m->instrument[i]->sample.memsegl, end);
            uint32_t smpl_para = (p_hi << 16) | p_lo;
            uint32_t smpl_offs = smpl_para << 4;
            
            if (smpl_offs == 0 || smpl_offs >= length) {
                s3m->sample[i] = NULL;
                continue;
            }
            s3m->sample[i] = &buffer[smpl_offs];
        } else {
            s3m->sample[i] = NULL;
        }
    }
}

static void load_pattern(s3m_t* s3m, uint8_t* buffer, size_t length)
{
    const uint8_t* end = buffer + length;
    uint16_t num_instr = read16((uint8_t*)&s3m->header->insnum, end);
    uint16_t num_pat = read16((uint8_t*)&s3m->header->patnum, end);
    if (num_pat > S3M_MAX_PATTERNS) num_pat = S3M_MAX_PATTERNS;
    
    uint16_t arrangement_len = read16((uint8_t*)&s3m->header->ordnum, end);
    uint32_t offs = 0x60 + arrangement_len + (num_instr * 2);  

    for (int i=0; i<num_pat; i++) {
        if (offs + 2 > length) break;
        uint32_t pat_offs = (uint32_t)read16(&buffer[offs], end) << 4;
        if (pat_offs == 0 || pat_offs > length - 2) {
            s3m->pattern[i] = NULL;
        } else {
            s3m->pattern[i] = &buffer[pat_offs];
        }
        offs += 2;
    }
}

int s3m_initialize(s3m_t* s3m, uint32_t samplerate)
{
    assert(s3m != NULL);
    memset(s3m, 0, sizeof(s3m_t));
    s3m->magic = S3M_MAGIC;
    s3m->samplerate = (samplerate > 0) ? samplerate : 44100;
    
    for (int i=0; i<S3M_VIBRATO_TABLE_SIZE; i++) {
        double vib_val = (sin(2*i*PI/S3M_VIBRATO_TABLE_SIZE) * 256.0);
        s3m->vibrato_table[i] = (int16_t)(vib_val + 0.5);
    }
    return 0;
}

void s3m_register_row_changed_callback(s3m_t* s3m, s3m_func_t func, void* arg)
{
    if (s3m && s3m->magic == S3M_MAGIC) {
        s3m->row_chg_callback = func;
        s3m->row_chg_callback_arg = arg;
    }
}

int s3m_from_ram(s3m_t* s3m, uint8_t* buffer, size_t length)
{
    if (!s3m || s3m->magic != S3M_MAGIC || !buffer || length < 0x60) return -1;
    if (memcmp(&buffer[0x2C], "SCRM", 4) != 0) return -1;

    s3m->buffer = buffer;
    s3m->filesize = length;
    s3m->header = (s3m_header_t*)s3m->buffer;
    
    uint16_t arrangement_len = read16((uint8_t*)&s3m->header->ordnum, buffer + length);
    if ((size_t)0x60 + arrangement_len > length) return -1;

    s3m->order = &s3m->buffer[0x60];
    load_instruments(s3m, s3m->buffer, s3m->filesize);
    load_samples(s3m, s3m->buffer, s3m->filesize);
    load_pattern(s3m, s3m->buffer, s3m->filesize);
               
    return 0;
}

void s3m_play(s3m_t* s3m) {
    s3m_play_at(s3m, 0);
}

void s3m_play_at(s3m_t* s3m, int order_idx) {
    if (!s3m || s3m->magic != S3M_MAGIC || !s3m->header || !s3m->order) return;
    const uint8_t* end = s3m->buffer + s3m->filesize;

    if (s3m->rt.playing == 0) {
        s3m__current_playing = s3m;
       
        s3m__set_tempo(s3m, s3m->header->start_tempo);
        s3m__set_speed(s3m, s3m->header->start_speed);
        
        s3m__set_global_vol(s3m, 64); // FORCED working volume

        uint8_t mvol = s3m->header->master_mult & 0x7F;
        if (mvol == 0) mvol = 64;
        s3m__set_master_vol(s3m, mvol);
        
        s3m->rt.frame_ctr = 0;
        s3m->rt.sample_ctr = 0;
        s3m->rt.order_idx = order_idx;
        s3m->rt.row_ctr = 0;
        s3m->rt.skip_rows = 0;
        
        uint16_t arrangement_len = read16((uint8_t*)&s3m->header->ordnum, end);
        while (s3m->rt.order_idx < arrangement_len && s3m->order[s3m->rt.order_idx] == 254) {
            s3m->rt.order_idx++;
        }

        if (s3m->rt.order_idx >= arrangement_len || s3m->order[s3m->rt.order_idx] == 255) {
            if (order_idx != 0) s3m_play_at(s3m, 0);
            return;
        }

        s3m->rt.pattern_idx = s3m->order[s3m->rt.order_idx];
        if (s3m->pattern[s3m->rt.pattern_idx] != NULL) {
            s3m->rt.pattern = &s3m->pattern[s3m->rt.pattern_idx][2];
        }

        for (int c=0; c<S3M_MAX_CHANNELS; c++) {
            chn_reset(&s3m->rt.chns[c]);
        }

        s3m->rt.playing = 1;
    }
}

void s3m_stop(s3m_t* s3m)
{
    if (s3m && s3m->magic == S3M_MAGIC) s3m->rt.playing = 0;
}

uint8_t s3m_get_current_pattern_idx(s3m_t* s3m)
{
    return (s3m && s3m->magic == S3M_MAGIC) ? s3m->rt.pattern_idx : 0;
}

uint8_t s3m_get_current_row_idx(s3m_t* s3m)
{
    return (s3m && s3m->magic == S3M_MAGIC) ? s3m->rt.row_ctr : 0;
}
