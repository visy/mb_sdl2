#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "s3m.h"

int s3m_load(s3m_t* s3m, const char* filename)
{
    int retval = -1;
    FILE* s3m_file = NULL;
    size_t len = 0;
    uint8_t* temp_buffer = NULL;
    
    if (!s3m || !filename) return -1;
  
    s3m_file = fopen(filename, "rb");
    if (s3m_file == NULL) {
        printf("Error: Could not open file %s (errno=%d)\n", filename, errno);
        return -1;
    }

    fseek(s3m_file, 0, SEEK_END);
    size_t filesize = ftell(s3m_file);
    fseek(s3m_file, 0, SEEK_SET);

    if (filesize < 0x60) {
        printf("Error: File too small to be S3M: %zu bytes\n", filesize);
        fclose(s3m_file);
        return -1;
    }

    // Allocate with padding for safety
    temp_buffer = calloc(filesize + 65536, sizeof(uint8_t));
    if (temp_buffer == NULL) {
        printf("Error: Memory allocation failed for %zu bytes\n", filesize);
        fclose(s3m_file);
        return -1;
    }

    len = fread(temp_buffer, 1, filesize, s3m_file);
    fclose(s3m_file);

    if (len < filesize) {
        printf("Error: Read only %zu of %zu bytes\n", len, filesize);
        free(temp_buffer);
        return -1;
    }

    // Only update the actual s3m structure if parsing succeeds
    retval = s3m_from_ram(s3m, temp_buffer, filesize);
    
    if (retval < 0) {           
        free(temp_buffer);
    }
    
    return retval;
}

void s3m_unload(s3m_t* s3m)
{
    if (!s3m || s3m->magic != S3M_MAGIC) return;
    
    s3m->rt.playing = false;
    // Brief sleep or sync would go here if multi-threaded
    
    if (s3m->buffer != NULL) {
        free(s3m->buffer);
        s3m->buffer = NULL;
    }
    
    s3m->header = NULL;
    s3m->order = NULL;
    s3m->filesize = 0;
    for (int i=0; i<S3M_MAX_INSTRUMENTS; i++) {
        s3m->instrument[i] = NULL;
        s3m->sample[i] = NULL;
        s3m->instr_c4_incr[i] = 0;
    }
    for (int i=0; i<S3M_MAX_PATTERNS; i++) {
        s3m->pattern[i] = NULL;
    }
    memset(&s3m->rt, 0, sizeof(runtime_t));
}
