#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pattern.h"

bool pat_read_packed_entry(uint8_t** pd, pat_entry_t* entry, uint8_t* p_end)
{
    uint8_t byte;
    uint8_t *p;
    if (pd == NULL || *pd == NULL || p_end == NULL || *pd >= p_end) return false;
    assert(entry != NULL);

    entry->chn = 255;
    entry->note = 255;
    entry->instr = 0; // Default to 0 (none)
    entry->vol = 255;
    entry->cmd = 255;
    entry->info = 255;
    
    p = *pd;
    byte = *p++;
    if (byte == 0) {
        *pd = p;
        return false;
    }
    entry->chn = byte & 0x1F;
    if (byte & 0x20) {
        if (p + 2 > p_end) return false;
        entry->note = *p++;
        entry->instr = *p++;
    }
    if (byte & 0x40) {
        if (p + 1 > p_end) return false;
        entry->vol = *p++;
    }
    if (byte & 0x80) {
        if (p + 2 > p_end) return false;
        entry->cmd = *p++;
        entry->info = *p++;
    }        
    *pd = p;
    return true;
}

bool pat_read_row(uint8_t** pd, pat_row_t* row, uint8_t* p_end)
{
    pat_entry_t entry;
    if (pd == NULL || row == NULL || p_end == NULL || *pd >= p_end) return false;
    
    memset(row, 255, sizeof(pat_row_t));
    for (int i=0; i<S3M_MAX_CHANNELS; i++) row->entry_chn[i].instr = 0;

    if (!pat_read_packed_entry(pd, &entry, p_end)) return false;
    
    do {
        if (entry.chn < 32) {
            memcpy(&row->entry_chn[entry.chn], &entry, sizeof(entry));
        }
    } while (pat_read_packed_entry(pd, &entry, p_end));
    return true;
}

void pat_skip_rows(uint8_t** pd, uint8_t skip, uint8_t* p_end)
{
    pat_row_t row;
    if (pd == NULL || p_end == NULL) return;
    
    while (skip-- > 0 && *pd < p_end) {
        if (!pat_read_row(pd, &row, p_end)) break;
    }
}
