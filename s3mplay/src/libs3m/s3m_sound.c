#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "channel.h"
#include "pattern.h"
#include "s3m.h"
#include "s3m_intern.h"

static uint16_t read16(const uint8_t* p, const uint8_t* end) {
    if (!p || p + 2 > end) return 0;
    return p[0] | (p[1] << 8);
}

static void process_row(s3m_t* s3m)
{
    int c;
    pat_row_t       row;
    pat_entry_t*    e;
    channel_t*      chn;
    uint8_t*        p_end = s3m->buffer + s3m->filesize;
    
    if (s3m->rt.pattern == NULL || s3m->rt.pattern >= p_end || s3m->rt.pattern < s3m->buffer) {
        s3m->rt.playing = 0;
        return;
    }

    if (!pat_read_row(&s3m->rt.pattern, &row, p_end)) {
        // end
    }

    for (c=0; c<S3M_MAX_CHANNELS; c++) {
        if (s3m->header->channels[c] == 255) continue;
        
        e = &row.entry_chn[c];
        chn = &s3m->rt.chns[c];
        if (e->note != 255) {
            chn_play_note(s3m, chn, e->instr, e->note);
        }
        if (e->vol != 255) {
            chn_set_volume(s3m, chn, e->vol);
        }
        chn_do_fx(s3m, chn, e->cmd, e->info);
    }
      
    s3m->rt.row_ctr++;
    if (s3m->rt.row_ctr >= S3M_MAX_ROWS_PER_PATTERN) {
        s3m->rt.row_ctr = 0;
        s3m->rt.order_idx++;
        
        uint16_t arrangement_len = read16((uint8_t*)&s3m->header->ordnum, p_end);
        while (s3m->rt.order_idx < arrangement_len && s3m->order[s3m->rt.order_idx] == 254) {
            s3m->rt.order_idx++;
        }

        if (s3m->rt.order_idx >= arrangement_len || s3m->order[s3m->rt.order_idx] == 255) {
            s3m->rt.playing = 0;            
            return;
        }

        uint8_t next_pat = s3m->order[s3m->rt.order_idx];
        if (s3m->pattern[next_pat] == NULL) {
            s3m->rt.playing = 0;            
            return;
        }
        
        s3m->rt.pattern_idx = next_pat;
        uint16_t pat_len = read16(s3m->pattern[s3m->rt.pattern_idx], p_end);
        uint8_t* p_pat_end = s3m->pattern[s3m->rt.pattern_idx] + pat_len + 2;
        if (p_pat_end > p_end) p_pat_end = p_end;

        s3m->rt.pattern = &s3m->pattern[s3m->rt.pattern_idx][2];
        if (s3m->rt.skip_rows > 0) {
            pat_skip_rows(&s3m->rt.pattern, s3m->rt.skip_rows, p_pat_end);                
            s3m->rt.row_ctr += s3m->rt.skip_rows;
            s3m->rt.skip_rows = 0;
        }
    }

    if (s3m->row_chg_callback != NULL) {
        s3m->row_chg_callback(s3m, s3m->row_chg_callback_arg);
    }    
}

static void process_frame(s3m_t* s3m)
{
    int c;
    uint8_t speed = (s3m->rt.speed > 0) ? s3m->rt.speed : 6;

    if (s3m->rt.frame_ctr >= speed) {
        s3m->rt.frame_ctr = 0;
    }

    if (s3m->rt.frame_ctr == 0) {
        process_row(s3m);
        speed = (s3m->rt.speed > 0) ? s3m->rt.speed : 6;
    }
        
    for (c=0; c<S3M_MAX_CHANNELS; c++) {
        if (s3m->header->channels[c] == 255) continue;
        chn_do_fx_frame(s3m, &s3m->rt.chns[c], s3m->rt.frame_ctr);
    }
    
    s3m->rt.frame_ctr++;
}

static void mix_samples_of_channels(s3m_t* s3m, int16_t* l_sample, int16_t* r_sample)
{
    double ls = 0, rs = 0, volfact;
    int32_t l, r;
    int c;
    uint8_t ct;
    bool has_stereo = false;
    
    for (c=0; c<S3M_MAX_CHANNELS; c++) {
        ct = s3m->header->channels[c];
        if (ct != 255 && ct >= 8 && ct < 16) {
            has_stereo = true;
            break;
        }
    }

    for (c=0; c<S3M_MAX_CHANNELS; c++) {
        ct = s3m->header->channels[c];
        if (ct == 255) continue;

        double sample = (double)chn_get_sample(s3m, &s3m->rt.chns[c]);
        
        if (!has_stereo) {
            ls += sample;
            rs += sample;
        } else {
            if (ct < 8) ls += sample;
            else if (ct < 16) rs += sample;
        }
    }
    
    // Original working volume factor
    volfact = (double)s3m->rt.global_vol * (double)s3m->rt.master_vol / (64.0*64.0*2.0); 
    l = (int32_t)(ls * volfact);
    r = (int32_t)(rs * volfact);
    
    if (l < -32768) l = -32768;
    if (l > 32767) l = 32767;
    if (r < -32768) r = -32768;
    if (r > 32767) r = 32767;
    *l_sample = (int16_t)l;
    *r_sample = (int16_t)r;  
}

void s3m__set_tempo(s3m_t* s3m, uint8_t tempo)
{
    if (!s3m) return;
    if (tempo < 32) tempo = 125;
    s3m->rt.tempo = tempo;
    double sample_per_frame = 2.5 * (double)s3m->samplerate / (double)s3m->rt.tempo;
    s3m->rt.sample_per_frame = (uint32_t)(sample_per_frame + 0.5);
    if (s3m->rt.sample_per_frame == 0) s3m->rt.sample_per_frame = 1;
}

void s3m__set_speed(s3m_t* s3m, uint8_t speed)
{
    if (!s3m) return;
    if (speed == 0) speed = 6;
    s3m->rt.speed = speed;
}

void s3m__set_global_vol(s3m_t* s3m, uint8_t vol)
{
    if (!s3m) return;
    if (vol > 64) vol = 64;
    s3m->rt.global_vol = vol;
}

void s3m__set_master_vol(s3m_t* s3m, uint8_t vol)
{
    if (!s3m) return;
    s3m->rt.master_vol = vol & 0x7F;
}

void SDLCALL s3m_sound_callback(void* arg, uint8_t* streambuf, int bufferlength)
{
    s3m_t* s3m = (s3m_t*)arg;
    if (s3m == NULL || s3m->magic != S3M_MAGIC || !s3m->rt.playing || s3m->buffer == NULL || s3m->rt.sample_per_frame == 0) {
        memset(streambuf, 0, bufferlength);
        return;
    }

    int num_samples = bufferlength >> 2;
    int bi = 0;
    
    for (int i = 0; i < num_samples; i++) {
        if (!s3m->rt.playing) {
            memset(streambuf + bi, 0, bufferlength - bi);
            break;
        }

        if (s3m->rt.sample_ctr == 0) {
            process_frame(s3m);
            s3m->rt.sample_ctr = s3m->rt.sample_per_frame;
        }
        s3m->rt.sample_ctr--;
        
        int16_t l_sample = 0, r_sample = 0;
        mix_samples_of_channels(s3m, &l_sample, &r_sample);
        
        streambuf[bi++] = l_sample & 0xFF;
        streambuf[bi++] = (l_sample >> 8) & 0xFF;
        streambuf[bi++] = r_sample & 0xFF;
        streambuf[bi++] = (r_sample >> 8) & 0xFF;
    }
}
