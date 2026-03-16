#include "channel.h"
#include "s3m.h"
#include "s3m_intern.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint16_t g_note_period[12] = 
    { 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 907 };

static uint32_t read32(const uint8_t* p, const uint8_t* end) {
    if (!p || p + 4 > end) return 0;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void chn_reset(channel_t* chn)
{
    if (!chn) return;
    memset(chn, 0, sizeof(channel_t));
    chn->note = 255;
    chn->last_note = 255; 
    chn->vol = 64;
}

void chn_update_sample_increment(s3m_t* s3m, channel_t* chn)
{
    if (s3m == NULL || chn == NULL || chn->sam_period < 0.0001) return;
    double note_herz = 14317056.0 / chn->sam_period;
    if (s3m->samplerate > 0) {
        chn->sam_incr = note_herz / (double)s3m->samplerate;
    }
}

void chn_calc_note_incr(s3m_t* s3m, channel_t* chn, uint8_t note)
{
    if (s3m == NULL || chn == NULL || chn->pi == NULL) return;

    uint8_t o = note >> 4;
    uint8_t n = note & 0x0F;
    if (o > 7) o = 7;
    if (n > 11) n = 11;   

    const uint8_t* end = s3m->buffer + s3m->filesize;
    uint32_t c2spd = read32((uint8_t*)&chn->pi->sample.c2spd, end);
    double instr_c2spd = (double)c2spd;
    if (instr_c2spd < 1.0) instr_c2spd = 8363.0;

    // Correct S3M frequency math: freq = (14317056 / (period / instr_c2spd))
    // which simplifies to: period_logical = (8363 * 16 * note_constant >> octave) / instr_c2spd
    uint32_t period = (8363 * 16) * g_note_period[n];
    period >>= o;
    
    chn->sam_target_period = (double)period / instr_c2spd;
    if (!chn->do_tone_porta) {
        chn->sam_period = chn->sam_target_period;
        chn_update_sample_increment(s3m, chn);
    }
}

void chn_play_note(s3m_t* s3m, channel_t* chn, uint8_t instr, uint8_t note)
{   
    if (s3m == NULL || chn == NULL) return;

    if (note == 254) {
        chn->vol = 0;
        return;
    }

    if (note < 254) {
        if ((note & 0x0F) >= 12) return;

        if (instr > 0) {
            uint8_t idx = instr - 1;
            if (idx < S3M_MAX_INSTRUMENTS) {
                chn->pi = s3m->instrument[idx];
                chn->ps = s3m->sample[idx];
                if (chn->pi) {
                    if (chn->pi->type == 1) chn->vol = chn->pi->sample.volume;
                    else chn->vol = chn->pi->adlib.volume;
                }
            }
        }

        if (chn->pi == NULL || chn->ps == NULL) return;

        chn->last_note = chn->note;
        chn->note = note;
        if (!chn->do_tone_porta) chn->sam_pos = 0.0;
        chn_calc_note_incr(s3m, chn, note);
    }
}

void chn_set_volume(s3m_t* s3m, channel_t* chn, uint8_t vol)
{
    (void)s3m;
    if (!chn) return;
    if (vol > 64) vol = 64;
    chn->vol = vol;
}

void chn_do_fx(s3m_t* s3m, channel_t* chn, uint8_t cmd, uint8_t param)
{
    if (!s3m || !chn) return;
    
    if (cmd != 'G'-64 && cmd != 'L'-64) chn->do_tone_porta = 0;
    chn->do_tone_slide = 0;

    switch (cmd) {
    case 'A'-64: s3m__set_speed(s3m, param); break;
    case 'B'-64: 
        s3m->rt.order_idx = param - 1; 
        s3m->rt.row_ctr = S3M_MAX_ROWS_PER_PATTERN; 
        break;
    case 'C'-64: {
        uint8_t row = ((param >> 4) * 10) + (param & 0x0F);
        s3m->rt.row_ctr = S3M_MAX_ROWS_PER_PATTERN;
        if (row >= S3M_MAX_ROWS_PER_PATTERN) row = S3M_MAX_ROWS_PER_PATTERN - 1;
        s3m->rt.skip_rows = row;
        break;
    }
    case 'E'-64:
        if (param) chn->porta_mem = param;
        param = chn->porta_mem;
        if (param < 0xE0) {
            chn->do_tone_slide = 1;
            chn->tone_slide_speed = (double)param * 4.0;
        } else if (param < 0xF0) {
            chn->sam_period += (double)(param & 0x0F);
            chn_update_sample_increment(s3m, chn);
        } else {
            chn->sam_period += (double)(param & 0x0F) * 4.0;
            chn_update_sample_increment(s3m, chn);
        }
        break;
    case 'F'-64:
        if (param) chn->porta_mem = param;
        param = chn->porta_mem;
        if (param < 0xE0) {
            chn->do_tone_slide = 1;
            chn->tone_slide_speed = -(double)param * 4.0;
        } else if (param < 0xF0) {
            chn->sam_period -= (double)(param & 0x0F);
            if (chn->sam_period < 1.0) chn->sam_period = 1.0;
            chn_update_sample_increment(s3m, chn);
        } else {
            chn->sam_period -= (double)(param & 0x0F) * 4.0;
            if (chn->sam_period < 1.0) chn->sam_period = 1.0;
            chn_update_sample_increment(s3m, chn);
        }
        break;
    case 'G'-64:
        if (param) chn->tone_porta_mem = param;
        chn->do_tone_porta = 1;
        chn->tone_slide_speed = (double)chn->tone_porta_mem * 4.0;
        break;
    case 'V'-64: s3m__set_global_vol(s3m, param); break;
    case 'T'-64: if (param >= 0x20) s3m__set_tempo(s3m, param); break;
    default: break;
    }
}

void chn_do_fx_frame(s3m_t* s3m, channel_t* chn, uint8_t tick)
{
    if (!s3m || !chn || tick == 0) return;

    if (chn->do_tone_slide) {
        chn->sam_period += chn->tone_slide_speed;
        if (chn->sam_period < 1.0) chn->sam_period = 1.0;
        chn_update_sample_increment(s3m, chn);
    } else if (chn->do_tone_porta) {
        if (chn->sam_period < chn->sam_target_period) {
            chn->sam_period += chn->tone_slide_speed;
            if (chn->sam_period > chn->sam_target_period) chn->sam_period = chn->sam_target_period;
        } else if (chn->sam_period > chn->sam_target_period) {
            chn->sam_period -= chn->tone_slide_speed;
            if (chn->sam_period < chn->sam_target_period) chn->sam_period = chn->sam_target_period;
        }
        chn_update_sample_increment(s3m, chn);
    }
}

int16_t chn_get_sample(s3m_t* s3m, channel_t* chn)
{
    if (!s3m || !chn || !chn->pi || !chn->ps || chn->vol == 0 || !s3m->buffer) return 0;
    
    const uint8_t* end = s3m->buffer + s3m->filesize;
    uint32_t smpl_len = read32((uint8_t*)&chn->pi->sample.length, end);
    if (smpl_len == 0) return 0;

    uint32_t spos = (uint32_t)(chn->sam_pos); 
    if (spos >= smpl_len) return 0;

    if (chn->ps < s3m->buffer || chn->ps >= end) return 0;
    if (chn->ps + spos < s3m->buffer || chn->ps + spos >= end) return 0;

    int16_t sample = (int16_t)(chn->ps[spos]) - 128;
    
    chn->sam_pos += chn->sam_incr;
    
    if (chn->pi->sample.flags & S3M_FLAG_INSTR_LOOP) {
        uint32_t lbegin = read32((uint8_t*)&chn->pi->sample.loopbeg, end);
        uint32_t lend = read32((uint8_t*)&chn->pi->sample.loopend, end);
        if (lend > lbegin && lend > 0 && lend <= smpl_len) {
            uint32_t next_spos = (uint32_t)(chn->sam_pos);
            if (next_spos >= lend) {
                chn->sam_pos = (double)lbegin + (chn->sam_pos - (double)lend);
            }
        }
    }
    
    return sample * chn->vol;
}
