#ifndef _CHANNEL_H_
#define _CHANNEL_H_

#include <stdbool.h>
#include <stdint.h>
#include "s3m.h"

void chn_reset(channel_t* chn);
void chn_update_sample_increment(s3m_t* s3m, channel_t* chn);
void chn_calc_note_incr(s3m_t* s3m, channel_t* chn, uint8_t note);
void chn_play_note(s3m_t* s3m, channel_t* chn, uint8_t instr, uint8_t note);
void chn_set_volume(s3m_t* s3m, channel_t* chn, uint8_t vol);
void chn_do_fx(s3m_t* s3m, channel_t* chn, uint8_t cmd, uint8_t param);
void chn_do_fx_frame(s3m_t* s3m, channel_t* chn, uint8_t tick);
int16_t chn_get_sample(s3m_t* s3m, channel_t* chn);

#endif
