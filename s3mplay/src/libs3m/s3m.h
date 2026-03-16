#ifndef _S3M_H_
#define _S3M_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

#define S3M_MAX_INSTRUMENTS             256
#define S3M_MAX_PATTERNS                256
#define S3M_MAX_ROWS_PER_PATTERN        64
#define S3M_MAX_SONG_NAME               28
#define S3M_MAX_CHANNELS                32
#define S3M_VIBRATO_TABLE_SIZE          64

#define S3M_FLAG_INSTR_LOOP             (1<<0)
#define S3M_MAGIC                       0x53334D50 

typedef struct _s3m_header {
    char            song_name[28];      // 0x00
    uint8_t         t1a;                // 0x1C
    uint8_t         type;               // 0x1D
    uint8_t         unused1[2];         // 0x1E
    uint16_t        ordnum;             // 0x20
    uint16_t        insnum;             // 0x22
    uint16_t        patnum;             // 0x24
    uint16_t        flags;              // 0x26
    uint16_t        tracker;            // 0x28
    uint16_t        fileformat;         // 0x2A
    char            scrm[4];            // 0x2C
    uint8_t         master_vol;         // 0x30
    uint8_t         start_speed;        // 0x31
    uint8_t         start_tempo;        // 0x32
    uint8_t         master_mult;        // 0x33
    uint8_t         ultraclick;         // 0x34
    uint8_t         pantable;           // 0x35
    uint8_t         unused2[8];         // 0x36
    uint16_t        special;            // 0x3E
    uint8_t         channels[32];       // 0x40
} s3m_header_t;

typedef struct _s3m_sample {
    uint8_t         type;           // 0x00
    char            filename[12];   // 0x01
    uint8_t         memsegh;        // 0x0D (High byte)
    uint16_t        memsegl;        // 0x0E (Low word)
    uint32_t        length;         // 0x10
    uint32_t        loopbeg;        // 0x14
    uint32_t        loopend;        // 0x18
    uint8_t         volume;         // 0x1C
    uint8_t         dsk;            // 0x1D
    uint8_t         pack;           // 0x1E
    uint8_t         flags;          // 0x1F
    uint32_t        c2spd;          // 0x20
    uint8_t         unused[12];     // 0x24
    char            sampname[28];   // 0x30
    char            scrs[4];        // 0x4C
} s3m_sample_t;

typedef struct _s3m_adlib {
    uint8_t         type;
    char            filename[12];
    uint8_t         memsegh;
    uint16_t        memsegl;
    uint8_t         d[12];
    uint8_t         volume;
    uint8_t         dsk;
    uint8_t         unused2[2];
    uint32_t        c4_speed;
    uint8_t         unused3[12];
    char            name[28];
    char            ident[4];
} s3m_adlib_t;

typedef union {
    uint8_t         type;
    s3m_sample_t    sample;
    s3m_adlib_t     adlib;
} s3m_instrument_t;

typedef struct _pat_entry {
    uint8_t     chn;
    uint8_t     note;
    uint8_t     instr;
    uint8_t     vol;
    uint8_t     cmd;
    uint8_t     info;  
} pat_entry_t;

typedef struct _pat_row {
    pat_entry_t entry_chn[S3M_MAX_CHANNELS];
} pat_row_t;

#pragma pack(pop)

typedef struct _channel {
    uint8_t             note;
    uint8_t             last_note;
    uint8_t             instr;
    s3m_instrument_t*   pi;
    uint8_t*            ps;
    int16_t             vol;
    double              sam_pos;
    double              sam_period;
    double              sam_target_period;
    double              sam_last_period;
    double              sam_incr;
    
    uint8_t             do_tone_slide;
    uint8_t             do_tone_porta;
    double              tone_slide_speed;
    
    // Effect Memory
    uint8_t             porta_mem;
    uint8_t             tone_porta_mem;
    uint8_t             vol_slide_mem;
    uint8_t             param;
} channel_t;

typedef struct _runtime {
    uint8_t         tempo;
    uint8_t         speed;
    uint8_t         global_vol;
    uint8_t         master_vol;
    uint8_t         playing;
    uint32_t        sample_ctr;
    uint32_t        sample_per_frame;
    uint8_t         frame_ctr;
    uint8_t         pattern_idx;
    uint8_t*        pattern;
    uint8_t         row_ctr;
    uint8_t         skip_rows;
    uint8_t         order_idx;
    channel_t       chns[S3M_MAX_CHANNELS];
} runtime_t;

struct _s3m;
typedef void (*s3m_func_t)(struct _s3m* s3m, void* arg);

typedef struct _s3m {
    uint32_t            magic;
    uint8_t*            buffer;
    size_t              filesize;
    uint32_t            samplerate;
    int16_t             vibrato_table[S3M_VIBRATO_TABLE_SIZE];
    s3m_header_t*       header;
    s3m_instrument_t*   instrument[S3M_MAX_INSTRUMENTS];   
    double              instr_c4_incr[S3M_MAX_INSTRUMENTS];
    uint8_t*            sample[S3M_MAX_INSTRUMENTS];
    uint8_t*            pattern[S3M_MAX_PATTERNS];
    uint8_t*            order;
    runtime_t           rt;
    s3m_func_t          row_chg_callback;
    s3m_func_t          row_chg_callback_arg;
} s3m_t;

int s3m_initialize(s3m_t* s3m, uint32_t samplerate);
void SDLCALL s3m_sound_callback(void* arg, uint8_t* streambuf, int bufferlength);
int s3m_from_ram(s3m_t* s3m, uint8_t* buffer, size_t length);
int s3m_load(s3m_t* s3m, const char* filename);
void s3m_unload(s3m_t* s3m);
void s3m_play(s3m_t* s3m);
void s3m_play_at(s3m_t* s3m, int order_idx);
void s3m_stop(s3m_t* s3m);
uint8_t s3m_get_current_pattern_idx(s3m_t* s3m);
uint8_t s3m_get_current_row_idx(s3m_t* s3m);
void s3m_register_row_changed_callback(s3m_t* s3m, s3m_func_t func, void* arg);

#ifdef __cplusplus
}
#endif

#endif
