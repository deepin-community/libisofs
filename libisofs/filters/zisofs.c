/*
 * Copyright (c) 2009 - 2020 Thomas Schmitt
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 *
 * It implements a filter facility which can pipe a IsoStream into zisofs
 * compression resp. uncompression, read its output and forward it as IsoStream
 * output to an IsoFile.
 * The zisofs format was invented by H. Peter Anvin. See doc/zisofs_format.txt
 * It is writeable and readable by zisofs-tools, readable by Linux kernels.
 *
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "../libisofs.h"
#include "../filter.h"
#include "../fsource.h"
#include "../util.h"
#include "../stream.h"
#include "../messages.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifdef Libisofs_with_zliB
#include <zlib.h>
#else
/* If zlib is not available then this code is a dummy */
#endif


/*
 * A filter that encodes or decodes the content of zisofs compressed files.
 */


/* The lowest size of a file which shall not be represented by zisofs v1 */
#define ISO_ZISOFS_V1_LIMIT 4294967296

/* zisofs2: Test value for small mixed-version ISOs: 1 million
            ISO_ZISOFS_V1_LIMIT 1000000
*/

/* Minimum and maximum blocks sizes for version 1 and 2 */
#define ISO_ZISOFS_V1_MIN_LOG2   15
#define ISO_ZISOFS_V1_MAX_LOG2   17
#define ISO_ZISOFS_V2_MIN_LOG2   15
#define ISO_ZISOFS_V2_MAX_LOG2   20


/* ------------------- Defaults of runtime parameters ------------------ */

/* Limit for overall count of allocated block pointers:
   2 exp 25 = 256 MiB blocklist buffer = 4 TiB uncompressed at 128 KiB
*/
#define ISO_ZISOFS_MAX_BLOCKS_T 0x2000000

/* Function to account for global block pointers */
static uint64_t ziso_block_pointer_mgt(uint64_t num, int mode);

/* Limit for single files:
   2 exp 25 = 256 MiB blocklist buffer = 4 TiB uncompressed at 128 KiB
*/
#define ISO_ZISOFS_MAX_BLOCKS_F 0x2000000

/* The number of blocks from which on the block pointer list shall be discarded
 * on iso_stream_close() of a compressing stream. This means that the pointers
 * have to be determined again on next ziso_stream_compress(), so that adding
 * a zisofs compression filter and writing the compressed stream needs in the
 * sum three read runs of the input stream.
 * <= 0 disables this file size based discarding.
 */ 
#define ISO_ZISOFS_MANY_BLOCKS 0

/* A ratio describing the part of the maximum number of block pointers which
 * shall be kept free by intermediate discarding of block pointers. See above
 * ISO_ZISOFS_MANY_BLOCKS. -1.0 disables this feature.
 */
#define ISO_ZISOFS_KBF_RATIO  -1.0 


/* --------------------------- Runtime parameters ------------------------- */

/* Sizes to be used for compression. Decompression learns from input header. */
static uint8_t ziso_block_size_log2 = 15;

static int ziso_v2_enabled = 0;
static int ziso_v2_block_size_log2 = 17;

static int64_t ziso_block_number_target = -1;

static int64_t ziso_max_total_blocks = ISO_ZISOFS_MAX_BLOCKS_T;
static int64_t ziso_max_file_blocks = ISO_ZISOFS_MAX_BLOCKS_F;

static int64_t ziso_many_block_limit = ISO_ZISOFS_MANY_BLOCKS;
static double ziso_keep_blocks_free_ratio = ISO_ZISOFS_KBF_RATIO;

/* Discard block pointers on last stream close even if the size constraints
 * are not met. To be set to 1 at block pointer overflow. To be set to 0
 * when all compression filters are deleted.
 */
static int ziso_early_bpt_discard = 0;

/* 1 = produce Z2 entries for zisofs2 , 0 = produce ZF for zisofs2
 * This is used as extern variable in rockridge.c
 */
int iso_zisofs2_enable_susp_z2 = 0;


static
int ziso_decide_v2_usage(off_t orig_size)
{
    if (ziso_v2_enabled > 1 ||
        (ziso_v2_enabled == 1 && orig_size >= (off_t) ISO_ZISOFS_V1_LIMIT))
        return 1;
    return 0;
}

static
int ziso_decide_bs_log2(off_t orig_size)
{       
    int bs_log2, bs_log2_min, i;
    off_t bs;

    if (ziso_decide_v2_usage(orig_size)) {
        bs_log2 = ziso_v2_block_size_log2;
        bs_log2_min = ISO_ZISOFS_V2_MIN_LOG2;
    } else {
        bs_log2 = ziso_block_size_log2;
        bs_log2_min = ISO_ZISOFS_V1_MIN_LOG2;
    }
    if (ziso_block_number_target <= 0)
        return bs_log2;

    for (i = bs_log2_min; i < bs_log2; i++) {
        bs = (1 << i);
        if (orig_size / bs + !!(orig_size % bs) + 1 <=
            ziso_block_number_target)
            return i;
    }
    return bs_log2;
}


/* --------------------------- ZisofsFilterRuntime ------------------------- */


/* Individual runtime properties exist only as long as the stream is opened.
 */
typedef struct
{
    int state; /* processing: 0= header, 1= block pointers, 2= data blocks */

    int zisofs_version; /* 1 or 2 */

    int block_size;
    int64_t block_pointer_fill;
    int64_t block_pointer_rpos;
    uint64_t *block_pointers; /* These are in use only with uncompression.
                                 Compression streams hold the pointer in
                                 their persistent data.
                               */

    char *read_buffer;
    char *block_buffer;
    int buffer_size;
    int buffer_fill;
    int buffer_rpos;

    off_t block_counter;
    off_t in_counter;
    off_t out_counter;

    int error_ret;

} ZisofsFilterRuntime;


static
int ziso_running_destroy(ZisofsFilterRuntime **running, int flag)
{
    ZisofsFilterRuntime *o= *running;
    if (o == NULL)
        return 0;
    if (o->block_pointers != NULL) {
        ziso_block_pointer_mgt((uint64_t) o->block_pointer_fill, 2);
        free(o->block_pointers);
    }
    if (o->read_buffer != NULL)
        free(o->read_buffer);
    if (o->block_buffer != NULL)
        free(o->block_buffer);
    free((char *) o);
    *running = NULL;
    return 1;
}


/*
 * @param flag bit0= do not set block_size, do not allocate buffers
 */
static
int ziso_running_new(ZisofsFilterRuntime **running, off_t orig_size,
                     int flag)
{
    ZisofsFilterRuntime *o;
    *running = o = calloc(sizeof(ZisofsFilterRuntime), 1);
    if (o == NULL) {
        return ISO_OUT_OF_MEM;
    }
    o->state = 0;
    o->block_size= 0;
    o->zisofs_version = 0;
    o->block_pointer_fill = 0;
    o->block_pointer_rpos = 0;
    o->block_pointers = NULL;
    o->read_buffer = NULL;
    o->block_buffer = NULL;
    o->buffer_size = 0;
    o->buffer_fill = 0;
    o->buffer_rpos = 0;
    o->block_counter = 0;
    o->in_counter = 0;
    o->out_counter = 0;
    o->error_ret = 0;

    if (flag & 1)
        return 1;

    o->block_size = (1 << ziso_decide_bs_log2(orig_size));
#ifdef Libisofs_with_zliB
    o->buffer_size = compressBound((uLong) o->block_size);
#else
    o->buffer_size = 2 * o->block_size;
#endif
    o->read_buffer = calloc(o->block_size, 1);
    o->block_buffer = calloc(o->buffer_size, 1);
    if (o->block_buffer == NULL || o->read_buffer == NULL)
        goto failed;
    return 1;
failed:
    ziso_running_destroy(running, 0);
    return -1;
}


/* --------------------------- Resource accounting ------------------------- */

/* @param mode  0= inquire whether num block pointers would fit
                1= register num block pointers
                2= unregister num block_pointers
                3= return number of accounted block pointers
   @return      if not mode 3: 0= does not fit , 1= fits
*/
static
uint64_t ziso_block_pointer_mgt(uint64_t num, int mode)
{
    static uint64_t global_count = 0;
    static int underrun = 0;

    if (mode == 2) {
        if (global_count < num) {
            if (underrun < 3)
                iso_msg_submit(-1, ISO_ZISOFS_BPT_UNDERRUN, 0, 
                            "Prevented global block pointer counter underrun");
            underrun++;
            global_count = 0;
        } else {
            global_count -= num;
        }
    } else if (mode == 3) {
        return global_count;
    } else {
       if (global_count + num > (uint64_t) ziso_max_total_blocks)
           return 0;
       if (mode == 1)
           global_count += num;
    }
    return 1;
}


/* ---------------------------- ZisofsFilterStreamData --------------------- */

/* The first 8 bytes of a zisofs compressed data file */
static unsigned char zisofs_magic[9] =
                              {0x37, 0xE4, 0x53, 0x96, 0xC9, 0xDB, 0xD6, 0x07};

/* The first 8 bytes of a zisofs2 compressed data file */
static unsigned char zisofs2_magic[9] =
                              {0xEF, 0x22, 0x55, 0xA1, 0xBC, 0x1B, 0x95, 0xA0};

/* Counts the number of active compression filters */
static off_t ziso_ref_count = 0;

/* Counts the number of active uncompression filters */
static off_t ziso_osiz_ref_count = 0;


#ifdef Libisofs_with_zliB
/* Parameter for compress2() , see <zlib.h> */

static int ziso_compression_level = 6;

#endif /* Libisofs_with_zliB */


/*
 * The common data payload of an individual Zisofs Filter IsoStream
 * IMPORTANT: Any change must be reflected by ziso_clone_stream().
 */
typedef struct
{
    IsoStream *orig;

    off_t size; /* -1 means that the size is unknown yet */

    ZisofsFilterRuntime *running; /* is non-NULL when open */

    ino_t id;

} ZisofsFilterStreamData;


/*
 * The data payload of an individual Zisofs Filter Compressor IsoStream
 * IMPORTANT: Any change must be reflected by ziso_clone_stream().
 */
typedef struct
{   
    ZisofsFilterStreamData std;

    uint64_t orig_size;
    uint64_t *block_pointers; /* Cache for output block addresses. They get
                                 written before the data and so need 2 passes.
                                 This cache avoids surplus passes.
                               */
    uint64_t block_pointer_counter;
    uint64_t open_counter;
    int block_pointers_dropped;

} ZisofsComprStreamData;


/*
 * The data payload of an individual Zisofs Filter Uncompressor IsoStream
 * IMPORTANT: Any change must be reflected by ziso_clone_stream().
 */
typedef struct
{
    ZisofsFilterStreamData std;

    uint8_t zisofs_algo_num;
    unsigned char header_size_div4;
    unsigned char block_size_log2;

} ZisofsUncomprStreamData;


/* Each individual ZisofsFilterStreamData needs a unique id number. */
/* >>> This is very suboptimal:
       The counter can rollover.
*/
static ino_t ziso_ino_id = 0;


/*
 * Methods for the IsoStreamIface of an Zisofs Filter object.
 */

static
int ziso_stream_uncompress(IsoStream *stream, void *buf, size_t desired);

static
int ziso_stream_compress(IsoStream *stream, void *buf, size_t desired);


/*
 * @param flag  bit0= discard even if the size conditions are not met
                bit1= check for open_counter == 1 rather than == 0
 */
static
int ziso_discard_bpt(IsoStream *stream, int flag)
{
    ZisofsFilterStreamData *data;
    ZisofsComprStreamData *cstd = NULL;
    int block_size;
    double max_blocks, free_blocks;

    data = stream->data;
    if (stream->class->read == &ziso_stream_compress)
        cstd = (ZisofsComprStreamData *) data;
    if (cstd == NULL)
        return 0;

    block_size = (1 << ziso_decide_bs_log2(cstd->orig_size));
    max_blocks = ziso_max_file_blocks;
    if (max_blocks < 1.0)
        max_blocks = 1.0;
    free_blocks = ziso_max_total_blocks -
                  ziso_block_pointer_mgt((uint64_t) 0, 3);
    if (cstd->block_pointers == NULL) {
        return 0;
    } else if (cstd->open_counter != !!(flag & 2)) {
        return 0;
    } else if (!((flag & 1) || ziso_early_bpt_discard)) {
        if (ziso_many_block_limit <= 0 ||
            cstd->orig_size / block_size + !!(cstd->orig_size % block_size)
            + 1 < (uint64_t) ziso_many_block_limit)
            if (ziso_keep_blocks_free_ratio < 0.0 ||
                free_blocks / max_blocks >= ziso_keep_blocks_free_ratio)
                return 0;
    }
    ziso_block_pointer_mgt(cstd->block_pointer_counter, 2);
    free((char *) cstd->block_pointers);
    cstd->block_pointers_dropped = 1;
    cstd->block_pointers = NULL;
    cstd->block_pointer_counter = 0;
    return 1;
}

/*
 * @param flag  bit0= original stream is not open
 *              bit1= do not destroy large
 *                    ZisofsComprStreamData->block_pointers
 */
static
int ziso_stream_close_flag(IsoStream *stream, int flag)
{
    ZisofsFilterStreamData *data;
    ZisofsComprStreamData *cstd = NULL;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = stream->data;
    if (stream->class->read == &ziso_stream_compress)
        cstd = (ZisofsComprStreamData *) data;

    if (cstd != NULL && !(flag & 2))
        ziso_discard_bpt(stream, 2);

    if (data->running == NULL) {
        return 1;
    }
    ziso_running_destroy(&(data->running), 0);
    if (flag & 1)
        return 1;
    if (cstd != NULL)
        if (cstd->open_counter > 0)
            cstd->open_counter--;
    return iso_stream_close(data->orig);
}


static
int ziso_stream_close(IsoStream *stream)
{
    return ziso_stream_close_flag(stream, 0);
}


/*
 * @param flag  bit0= do not run .get_size() if size is < 0
 */
static
int ziso_stream_open_flag(IsoStream *stream, int flag)
{
    ZisofsFilterStreamData *data;
    ZisofsComprStreamData *cstd;
    ZisofsFilterRuntime *running = NULL;
    int ret;
    off_t orig_size = 0;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = (ZisofsFilterStreamData*) stream->data;
    if (data->running != NULL) {
        return ISO_FILE_ALREADY_OPENED;
    }
    if (data->size < 0 && !(flag & 1)) {
        /* Do the size determination run now, so that the size gets cached
           and .get_size() will not fail on an opened stream.
        */
        stream->class->get_size(stream);
    }
    orig_size = data->size;
    if (stream->class->read == &ziso_stream_compress) {
        cstd = (ZisofsComprStreamData *) data;
        cstd->open_counter++;
        orig_size = cstd->orig_size;
    }
    if (orig_size < 0)
        return ISO_ZISOFS_UNKNOWN_SIZE;

    ret = ziso_running_new(&running, orig_size,
                           (stream->class->read == &ziso_stream_uncompress));
    if (ret < 0) {
        return ret;
    }
    data->running = running;

    ret = iso_stream_open(data->orig);
    if (ret < 0) {
        return ret;
    }
    return 1;
}


static
int ziso_stream_open(IsoStream *stream)
{
    return ziso_stream_open_flag(stream, 0);
}


/* @param flag bit0= stream is already open
               bit1= close stream with flag bit1
 */
static
off_t ziso_stream_measure_size(IsoStream *stream, int flag)
{
    int ret, ret_close;
    off_t count = 0;
    ZisofsFilterStreamData *data;
    char buf[64 * 1024];
    size_t bufsize = 64 * 1024;

    if (stream == NULL)
        return ISO_NULL_POINTER;
    data = stream->data;

    /* Run filter command and count output bytes */
    if (!(flag & 1)) {
        ret = ziso_stream_open_flag(stream, 1);
        if (ret < 0)
            return ret;
    }
    if (stream->class->read == &ziso_stream_uncompress) {
        /* It is enough to read the header part of a compressed file */
        ret = ziso_stream_uncompress(stream, buf, 0);
        count = data->size;
    } else {
        /* The size of the compression result has to be counted */
        while (1) {
            ret = stream->class->read(stream, buf, bufsize);
            if (ret <= 0)
        break;
            count += ret;
        }
    }
    ret_close = ziso_stream_close_flag(stream, flag & 2);
    if (ret < 0)
        return ret;
    if (ret_close < 0)
        return ret_close;

    data->size = count;
    return count;
}


static
int ziso_stream_compress(IsoStream *stream, void *buf, size_t desired)
{

#ifdef Libisofs_with_zliB

    int ret, todo, i;
    ZisofsComprStreamData *data;
    ZisofsFilterRuntime *rng;
    size_t fill = 0;
    off_t orig_size, next_pt, measure_ret;
    char *cbuf = buf;
    uLongf buf_len;
    uint64_t *copy_base, num_blocks = 0;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = stream->data;
    rng= data->std.running;
    if (rng == NULL) {
        return ISO_FILE_NOT_OPENED;
    }
    if (rng->error_ret < 0) {
        return rng->error_ret;
    }

    if (data->block_pointers_dropped) {
        /* The list was dropped after measurement of compressed size. But this
         * run of the function expects it as already filled with pointer
         * values. So now they have to be re-computed by extra runs of this
         * function in the course of compressed size measurement.
         */
        data->block_pointers_dropped = 0;
        measure_ret = ziso_stream_measure_size(stream, 1 | 2); 
        if (measure_ret < 0)
            return (rng->error_ret = measure_ret);

        /* Stream was closed. Open it again, without any size determination. */
        ret = ziso_stream_open_flag(stream, 1);
        if (ret < 0)
            return ret;
    }

    while (1) {
        if (rng->state == 0) {
            /* Delivering file header */

            if (rng->buffer_fill == 0) {
                orig_size = iso_stream_get_size(data->std.orig);
                num_blocks = orig_size / rng->block_size +
                             1 + !!(orig_size % rng->block_size);
                if (num_blocks > (uint64_t) ziso_max_file_blocks)
                    return (rng->error_ret = ISO_ZISOFS_TOO_LARGE);
                if (ziso_block_pointer_mgt((uint64_t) num_blocks, 0) == 0) {
                    ziso_early_bpt_discard = 1;
                    return (rng->error_ret = ISO_ZISOFS_TOO_MANY_PTR);
                }
                if (orig_size != (off_t) data->orig_size)
                    return (rng->error_ret = ISO_FILTER_WRONG_INPUT);
                if (ziso_decide_v2_usage(orig_size)) {
                    rng->zisofs_version = 2;
                    memcpy(rng->block_buffer, zisofs2_magic, 8);
                    rng->block_buffer[8] = 0;    /* @hdr_version */
                    rng->block_buffer[9] = 6;    /* @hdr_size */
                    rng->block_buffer[10] = 1;   /* @alg_id */
                    rng->block_buffer[11] = ziso_decide_bs_log2(orig_size);
                    iso_lsb64((uint8_t *) (rng->block_buffer + 12),
                              (uint64_t) orig_size);
                    memset(rng->block_buffer + 20, 0, 4);
                    rng->buffer_fill = 24;
                } else {
                    if (orig_size >= (off_t) ISO_ZISOFS_V1_LIMIT) {
                        return (rng->error_ret = ISO_ZISOFS_TOO_LARGE);
                    }
                    rng->zisofs_version = 1;
                    memcpy(rng->block_buffer, zisofs_magic, 8);
                    iso_lsb((unsigned char *) (rng->block_buffer + 8),
                            (uint32_t) orig_size, 4);
                    rng->block_buffer[12] = 4;
                    rng->block_buffer[13] = ziso_decide_bs_log2(orig_size);
                    rng->block_buffer[14] = rng->block_buffer[15] = 0;
                    rng->buffer_fill = 16;
                }
                rng->buffer_rpos = 0;
            } else if (rng->buffer_rpos >= rng->buffer_fill) {
                rng->buffer_fill = rng->buffer_rpos = 0;
                rng->state = 1; /* header is delivered */
            }
        }
        if (rng->state == 1) {
            /* Delivering block pointers */;

            if (rng->block_pointer_fill == 0 || data->block_pointers == NULL) {
                /* Initialize block pointer writing */
                rng->block_pointer_rpos = 0;
                num_blocks = data->orig_size / rng->block_size
                             + 1 + !!(data->orig_size % rng->block_size);
                if (rng->block_pointer_fill > 0 &&
                    (int64_t) num_blocks != rng->block_pointer_fill)
                    return (rng->error_ret = ISO_FILTER_WRONG_INPUT);
                rng->block_pointer_fill = num_blocks;
                if (data->block_pointers == NULL) {
                    /* On the first pass, create pointer array with all 0s */
                    if (ziso_block_pointer_mgt(num_blocks, 1) == 0) {
                        rng->block_pointer_fill = 0;
                        ziso_early_bpt_discard = 1;
                        return (rng->error_ret = ISO_ZISOFS_TOO_MANY_PTR);
                    }
                    data->block_pointers = calloc(rng->block_pointer_fill, 8);
                    if (data->block_pointers == NULL) {
                        ziso_block_pointer_mgt(num_blocks, 2);
                        rng->block_pointer_fill = 0;
                        return (rng->error_ret = ISO_OUT_OF_MEM);
                    }
                    data->block_pointer_counter = rng->block_pointer_fill;
                }
            }

            if (rng->buffer_rpos >= rng->buffer_fill) {
                if (rng->block_pointer_rpos >= rng->block_pointer_fill) {
                    rng->buffer_fill = rng->buffer_rpos = 0;
                    rng->block_counter = 0;
                    if (rng->zisofs_version == 1)
                        data->block_pointers[0] = 16 +
                                                  rng->block_pointer_fill * 4;
                    else
                        data->block_pointers[0] = 24 +
                                                  rng->block_pointer_fill * 8;
                    rng->state = 2; /* block pointers are delivered */
                } else {
                    /* Provide a buffer full of block pointers */
                    /* data->block_pointers was filled by ziso_stream_open() */
                    todo = rng->block_pointer_fill - rng->block_pointer_rpos;
                    copy_base = data->block_pointers + rng->block_pointer_rpos;
                    if (rng->zisofs_version == 1) {
                        if (todo * 4 > rng->buffer_size)
                            todo = rng->buffer_size / 4;
                        for (i = 0; i < todo; i++)
                            iso_lsb((unsigned char *) (rng->block_buffer +
                                                       4 * i), 
                                    (uint32_t) (copy_base[i] & 0xffffffff), 4);
                        rng->buffer_fill = todo * 4;
                    } else {
                        if (todo * 8 > rng->buffer_size)
                            todo = rng->buffer_size / 8;
                        for (i = 0; i < todo; i++)
                            iso_lsb64((uint8_t *) rng->block_buffer + 8 * i,
                                      copy_base[i]);
                        rng->buffer_fill = todo * 8;
                    }
                    rng->buffer_rpos = 0;
                    rng->block_pointer_rpos += todo;
                }
            }
        }
        if (rng->state == 2 && rng->buffer_rpos >= rng->buffer_fill) {
            /* Delivering data blocks */;

            ret = iso_stream_read(data->std.orig, rng->read_buffer,
                                  rng->block_size);
            if (ret > 0) {
                rng->in_counter += ret;
                if ((uint64_t) rng->in_counter > data->orig_size) {
                    /* Input size became larger */
                    return (rng->error_ret = ISO_FILTER_WRONG_INPUT);
                }
                /* Check whether all 0 : represent as 0-length block */;
                for (i = 0; i < ret; i++)
                    if (rng->read_buffer[i])
                break;
                if (i >= ret) { /* All 0-bytes. Bypass compression. */
                    buf_len = 0;
                } else {
                    buf_len = rng->buffer_size;
                    ret = compress2((Bytef *) rng->block_buffer, &buf_len,
                                    (Bytef *) rng->read_buffer, (uLong) ret,
                                    ziso_compression_level);
                    if (ret != Z_OK) {
                        return (rng->error_ret = ISO_ZLIB_COMPR_ERR);
                    }
                }
                rng->buffer_fill = buf_len;
                rng->buffer_rpos = 0;

                next_pt = data->block_pointers[rng->block_counter] + buf_len;

                if (data->std.size >= 0 && next_pt > data->std.size) {
                    /* Compression yields more bytes than on first run */
                    return (rng->error_ret = ISO_FILTER_WRONG_INPUT);
                }

                /* Check or record check block pointer */
                rng->block_counter++;
                if (data->block_pointers[rng->block_counter] > 0) {
                    if ((uint64_t) next_pt !=
                        data->block_pointers[rng->block_counter]) {
                        /* block pointers mismatch , content has changed */
                        return (rng->error_ret = ISO_FILTER_WRONG_INPUT);
                    }
                } else {
                    data->block_pointers[rng->block_counter] = next_pt;
                }

            } else if (ret == 0) {
                rng->state = 3;
                if ((uint64_t) rng->in_counter != data->orig_size) {
                    /* Input size shrunk */
                    return (rng->error_ret = ISO_FILTER_WRONG_INPUT);
                }
                return fill;
            } else
                return (rng->error_ret = ret);
            if (rng->buffer_fill == 0) {
    continue;
            }
        }
        if (rng->state == 3 && rng->buffer_rpos >= rng->buffer_fill) {
            return 0; /* EOF */
        }

        /* Transfer from rng->block_buffer to buf */
        todo = desired - fill;
        if (todo > rng->buffer_fill - rng->buffer_rpos)
            todo = rng->buffer_fill - rng->buffer_rpos;
        memcpy(cbuf + fill, rng->block_buffer + rng->buffer_rpos, todo);
        fill += todo;
        rng->buffer_rpos += todo;
        rng->out_counter += todo;

        if (fill >= desired) {
           return fill;
        }
    }
    return ISO_FILE_READ_ERROR; /* should never be hit */

#else

    return ISO_ZLIB_NOT_ENABLED;

#endif

}


#ifdef Libisofs_with_zliB

static
int ziso_algo_to_num(uint8_t zisofs_algo[2])
{
    if (zisofs_algo[0] == 'p' && zisofs_algo[1] == 'z')
        return 0;
    if (zisofs_algo[0] == 'P' && zisofs_algo[1] == 'Z')
        return 1;
    if (zisofs_algo[0] == 'X' && zisofs_algo[1] == 'Z')
        return 2;
    if (zisofs_algo[0] == 'L' && zisofs_algo[1] == '4')
        return 3;
    if (zisofs_algo[0] == 'Z' && zisofs_algo[1] == 'D')
        return 4;
    if (zisofs_algo[0] == 'B' && zisofs_algo[1] == '2')
        return 5;
    return -1;
}

#endif /* Libisofs_with_zliB */

static
int ziso_num_to_algo(uint8_t num, uint8_t zisofs_algo[2])
{
    if (num == 0) {
        zisofs_algo[0] = 'p';
        zisofs_algo[1] = 'z';
        return 1;
    } else if (num == 1) {
        zisofs_algo[0] = 'P';
        zisofs_algo[1] = 'Z';
        return 1;
    } else if (num == 2) {
        zisofs_algo[0] = 'X';
        zisofs_algo[1] = 'Z';
        return 2;
    } else if (num == 3) {
        zisofs_algo[0] = 'L';
        zisofs_algo[1] = '4';
        return 2;
    } else if (num == 4) {
        zisofs_algo[0] = 'Z';
        zisofs_algo[1] = 'D';
        return 2;
    } else if (num == 5) {
        zisofs_algo[0] = 'B';
        zisofs_algo[1] = '2';
        return 2;
    }
    return -1;
}


/* @param flag bit0= recognize zisofs2 only if ziso_v2_enabled
               bit1= do not accept algorithms which libisofs does not support
*/
static
int ziso_parse_zisofs_head(IsoStream *stream, uint8_t *ziso_algo_num,
                           int *header_size_div4, int *block_size_log2,
                           uint64_t *uncompressed_size, int flag)
{
    int ret, consumed = 0, i;
    char zisofs_head[24];
    char waste_word[4];

    ret = iso_stream_read(stream, zisofs_head, 8);
    if (ret < 0)
        return ret;
    if (ret != 8)
        return ISO_ZISOFS_WRONG_INPUT;
    consumed = 8;
    if (memcmp(zisofs_head, zisofs_magic, 8) == 0) {
        *ziso_algo_num = 0;
        ret = iso_stream_read(stream, zisofs_head + 8, 8);
        if (ret < 0)
            return ret;
        if (ret != 8)
            return ISO_ZISOFS_WRONG_INPUT;
        consumed += 8;
        *header_size_div4 = ((unsigned char *) zisofs_head)[12];
        *block_size_log2 = ((unsigned char *) zisofs_head)[13];
        *uncompressed_size = iso_read_lsb(((uint8_t *) zisofs_head) + 8, 4);
        if (*header_size_div4 < 4 ||
            *block_size_log2 < ISO_ZISOFS_V1_MIN_LOG2 ||
            *block_size_log2 > ISO_ZISOFS_V1_MAX_LOG2)
            return ISO_ZISOFS_WRONG_INPUT;
    } else if (memcmp(zisofs_head, zisofs2_magic, 8) == 0 &&
               !(ziso_v2_enabled == 0 && (flag & 1))) {
        ret = iso_stream_read(stream, zisofs_head + 8, 16);
        if (ret < 0)
            return ret;
        if (ret != 16)
            return ISO_ZISOFS_WRONG_INPUT;
        consumed += 16;
        *ziso_algo_num = zisofs_head[10];
        *header_size_div4 = ((unsigned char *) zisofs_head)[9];
        *block_size_log2 = ((unsigned char *) zisofs_head)[11];
        *uncompressed_size = iso_read_lsb64(((uint8_t *) zisofs_head) + 12);
        if (*header_size_div4 < 4 ||
            *block_size_log2 < ISO_ZISOFS_V2_MIN_LOG2 ||
            *block_size_log2 > ISO_ZISOFS_V2_MAX_LOG2 ||
            (*ziso_algo_num != 1 && (flag & 2)))
            return ISO_ZISOFS_WRONG_INPUT;
    } else {
        return ISO_ZISOFS_WRONG_INPUT;
    }
    for (i = consumed; i < *header_size_div4; i++) {
       /* Skip surplus header words */
       ret = iso_stream_read(stream, waste_word, 4);
       if (ret < 0)
           return ret;
       if (ret != 4)
           return ISO_ZISOFS_WRONG_INPUT; 
    }
    return 1;
}


/* Note: A call with desired==0 directly after .open() only checks the file
         head and loads the uncompressed size from that head.
*/
static
int ziso_stream_uncompress(IsoStream *stream, void *buf, size_t desired)
{

#ifdef Libisofs_with_zliB

    int ret, todo, header_size, bs_log2, block_max = 1, blpt_size;
    ZisofsFilterStreamData *data;
    ZisofsFilterRuntime *rng;
    ZisofsUncomprStreamData *nstd;
    size_t fill = 0;
    char *cbuf = buf;
    uLongf buf_len;
    uint64_t uncompressed_size;
    int64_t i;
    uint8_t algo_num, *rpt, *wpt;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = stream->data;
    nstd = stream->data;
    rng= data->running;
    if (rng == NULL) {
        return ISO_FILE_NOT_OPENED;
    }
    if (rng->error_ret < 0) {
        return rng->error_ret;
    }

    while (1) {
        if (rng->state == 0) {
            /* Reading file header */
            ret = ziso_parse_zisofs_head(data->orig, &algo_num, &header_size,
                                         &bs_log2, &uncompressed_size, 2);
            if (ret < 0)
                return (rng->error_ret = ret);
            if (algo_num == 0)
                blpt_size = 4;
            else
                blpt_size = 8;
            nstd->header_size_div4 = header_size;
            header_size *= 4;
            data->size = uncompressed_size;
            nstd->block_size_log2 = bs_log2;
            rng->block_size = 1 << bs_log2;

            if (desired == 0)
                return 0;

            /* Create and read pointer array */
            rng->block_pointer_rpos = 0;
            rng->block_pointer_fill = data->size / rng->block_size
                                     + 1 + !!(data->size % rng->block_size);
            if (rng->block_pointer_fill > ziso_max_file_blocks) {
                rng->block_pointer_fill = 0;
                return (rng->error_ret = ISO_ZISOFS_TOO_LARGE);
            }
            if (ziso_block_pointer_mgt((uint64_t) rng->block_pointer_fill, 1)
                == 0)
                return ISO_ZISOFS_TOO_MANY_PTR;
            rng->block_pointers = calloc(rng->block_pointer_fill, 8);
            if (rng->block_pointers == NULL) {
                ziso_block_pointer_mgt((uint64_t) rng->block_pointer_fill, 2);
                rng->block_pointer_fill = 0;
                return (rng->error_ret = ISO_OUT_OF_MEM);
            }
            ret = iso_stream_read(data->orig, rng->block_pointers,
                                  rng->block_pointer_fill * blpt_size);
            if (ret < 0)
                return (rng->error_ret = ret);
            if (algo_num == 0) {
                /* Spread 4 byte little-endian pointer values over 8 byte */
                rpt = ((uint8_t *) rng->block_pointers)
                      + rng->block_pointer_fill * 4;
                wpt = ((uint8_t *) rng->block_pointers)
                      + rng->block_pointer_fill * 8;
		while (rpt > ((uint8_t *) rng->block_pointers) + 4) {
                    rpt -= 4;
                    wpt -= 8;
                    memcpy(wpt, rpt, 4);
                    memset(wpt + 4, 0, 4);
                }
                memset(((uint8_t *) rng->block_pointers) + 4, 0, 4);
            }
            if (ret != rng->block_pointer_fill * blpt_size)
               return (rng->error_ret = ISO_ZISOFS_WRONG_INPUT);
            for (i = 0; i < rng->block_pointer_fill; i++) {
                 rng->block_pointers[i] =
                         iso_read_lsb64((uint8_t *) (rng->block_pointers + i));
                 if (i > 0)
                     if ((int) (rng->block_pointers[i] -
                                rng->block_pointers[i - 1])
                         > block_max)
                         block_max = rng->block_pointers[i]
                                     - rng->block_pointers[i - 1];
            }

            rng->read_buffer = calloc(block_max, 1);
            rng->block_buffer = calloc(rng->block_size, 1);
            if (rng->read_buffer == NULL || rng->block_buffer == NULL)
                return (rng->error_ret = ISO_OUT_OF_MEM);
            rng->state = 2; /* block pointers are read */
            rng->buffer_fill = rng->buffer_rpos = 0;
        }

        if (rng->state == 2 && rng->buffer_rpos >= rng->buffer_fill) {
            /* Delivering data blocks */;
            i = ++(rng->block_pointer_rpos);
            if (i >= rng->block_pointer_fill) {
                if (rng->out_counter == data->size) {
                    rng->state = 3;
                    rng->block_pointer_rpos--;
                    return fill;
                }
                /* More data blocks needed than announced */
                return (rng->error_ret = ISO_FILTER_WRONG_INPUT);
            }
            todo = rng->block_pointers[i] - rng->block_pointers[i- 1];
            if (todo == 0) {
                memset(rng->block_buffer, 0, rng->block_size);
                rng->buffer_fill = rng->block_size;
                if (rng->out_counter + rng->buffer_fill > data->size &&
                    i == rng->block_pointer_fill - 1)
                    rng->buffer_fill = data->size - rng->out_counter;
            } else {
                ret = iso_stream_read(data->orig, rng->read_buffer, todo);
                if (ret > 0) {
                    rng->in_counter += ret;
                    buf_len = rng->block_size;
                    ret = uncompress((Bytef *) rng->block_buffer, &buf_len,
                                     (Bytef *) rng->read_buffer, (uLong) ret);
                    if (ret != Z_OK)
                        return (rng->error_ret = ISO_ZLIB_COMPR_ERR);
                    rng->buffer_fill = buf_len;
                    if ((int) buf_len < rng->block_size &&
                        i != rng->block_pointer_fill - 1)
                        return (rng->error_ret = ISO_ZISOFS_WRONG_INPUT);
                } else if(ret == 0) {
                    rng->state = 3;
                    if (rng->out_counter != data->size) {
                        /* Input size shrunk */
                        return (rng->error_ret = ISO_FILTER_WRONG_INPUT);
                    }
                    return fill;
                } else
                    return (rng->error_ret = ret);
            }
            rng->buffer_rpos = 0;

            if (rng->out_counter + rng->buffer_fill > data->size) {
                /* Uncompression yields more bytes than announced by header */
                return (rng->error_ret = ISO_FILTER_WRONG_INPUT);
            }
        }
        if (rng->state == 3 && rng->buffer_rpos >= rng->buffer_fill) {
            return 0; /* EOF */
        }

        /* Transfer from rng->block_buffer to buf */
        todo = desired - fill;
        if (todo > rng->buffer_fill - rng->buffer_rpos)
            todo = rng->buffer_fill - rng->buffer_rpos;
        memcpy(cbuf + fill, rng->block_buffer + rng->buffer_rpos, todo);
        fill += todo;
        rng->buffer_rpos += todo;
        rng->out_counter += todo;

        if (fill >= desired) {
           return fill;
        }
    }
    return (rng->error_ret = ISO_FILE_READ_ERROR); /* should never be hit */

#else

    return ISO_ZLIB_NOT_ENABLED;

#endif

}


static
off_t ziso_stream_get_size(IsoStream *stream)
{
    off_t ret;
    ZisofsFilterStreamData *data;

    if (stream == NULL)
        return ISO_NULL_POINTER;
    data = stream->data;
    if (data->size >= 0)
        return data->size;
    ret = ziso_stream_measure_size(stream, 0);
    return ret;
}


static
int ziso_stream_is_repeatable(IsoStream *stream)
{
    /* Only repeatable streams are accepted as orig */
    return 1;
}


static
void ziso_stream_get_id(IsoStream *stream, unsigned int *fs_id, 
                        dev_t *dev_id, ino_t *ino_id)
{
    ZisofsFilterStreamData *data;

    data = stream->data;
    *fs_id = ISO_FILTER_FS_ID;
    *dev_id = ISO_FILTER_ZISOFS_DEV_ID;
    *ino_id = data->id;
}


static
void ziso_stream_free(IsoStream *stream)
{
    ZisofsFilterStreamData *data;
    ZisofsComprStreamData *nstd;

    if (stream == NULL) {
        return;
    }
    data = stream->data;
    if (data->running != NULL) {
        ziso_stream_close(stream);
    }
    if (stream->class->read == &ziso_stream_uncompress) {
        if (--ziso_osiz_ref_count < 0)
            ziso_osiz_ref_count = 0;
    } else {
        nstd = stream->data;
        if (nstd->block_pointers != NULL) {
            ziso_block_pointer_mgt(nstd->block_pointer_counter, 2);
            free((char *) nstd->block_pointers);
        }
        if (--ziso_ref_count < 0)
            ziso_ref_count = 0;
        if (ziso_ref_count == 0)
            ziso_early_bpt_discard = 0;
    }
    iso_stream_unref(data->orig);
    free(data);
}


static
int ziso_update_size(IsoStream *stream)
{
    /* By principle size is determined only once */
    return 1;
}


static
IsoStream *ziso_get_input_stream(IsoStream *stream, int flag)
{
    ZisofsFilterStreamData *data;

    if (stream == NULL) {
        return NULL;
    }
    data = stream->data;
    return data->orig;
}

static
int ziso_clone_stream(IsoStream *old_stream, IsoStream **new_stream, int flag)
{
    int ret;
    IsoStream *new_input_stream = NULL, *stream = NULL;
    ZisofsFilterStreamData *stream_data, *old_stream_data;
    ZisofsUncomprStreamData *uncompr, *old_uncompr;
    ZisofsComprStreamData *compr, *old_compr;

    if (flag)
        return ISO_STREAM_NO_CLONE; /* unknown option required */

    ret = iso_stream_clone_filter_common(old_stream, &stream,
                                         &new_input_stream, 0);
    if (ret < 0)
        return ret;

    if (old_stream->class->read == &ziso_stream_uncompress) {
        uncompr = calloc(1, sizeof(ZisofsUncomprStreamData));
        if (uncompr == NULL)
            goto no_mem;
        stream_data = (ZisofsFilterStreamData *) uncompr;
        old_uncompr = (ZisofsUncomprStreamData *) old_stream->data;
        uncompr->zisofs_algo_num = old_uncompr->zisofs_algo_num;
        uncompr->header_size_div4 = old_uncompr->header_size_div4;
        uncompr->block_size_log2 = old_uncompr->block_size_log2;
    } else {
        compr = calloc(1, sizeof(ZisofsComprStreamData));
        if (compr == NULL)
            goto no_mem;
        stream_data = (ZisofsFilterStreamData *) compr;
        old_compr = (ZisofsComprStreamData *) old_stream->data;
        compr->orig_size = old_compr->orig_size;
        compr->block_pointers = NULL;
        compr->block_pointer_counter = 0;
        compr->open_counter = 0;
        if (old_compr->block_pointers != NULL ||
            old_compr->block_pointers_dropped)
            compr->block_pointers_dropped = 1;
        else
            compr->block_pointers_dropped = 0;
    }
    old_stream_data = (ZisofsFilterStreamData *) old_stream->data;
    stream_data->orig = new_input_stream;
    stream_data->size = old_stream_data->size;
    stream_data->running = NULL;
    stream_data->id = ++ziso_ino_id;
    stream->data = stream_data;
    *new_stream = stream;
    return ISO_SUCCESS;
no_mem:
    if (new_input_stream != NULL)
        iso_stream_unref(new_input_stream);
    if (stream != NULL)
        iso_stream_unref(stream);
    return ISO_OUT_OF_MEM;
}


static
int ziso_cmp_ino(IsoStream *s1, IsoStream *s2);

static
int ziso_uncompress_cmp_ino(IsoStream *s1, IsoStream *s2);


IsoStreamIface ziso_stream_compress_class = {
    4,
    "ziso",
    ziso_stream_open,
    ziso_stream_close,
    ziso_stream_get_size,
    ziso_stream_compress,
    ziso_stream_is_repeatable,
    ziso_stream_get_id,
    ziso_stream_free,
    ziso_update_size,
    ziso_get_input_stream,
    ziso_cmp_ino,
    ziso_clone_stream
};


IsoStreamIface ziso_stream_uncompress_class = {
    4,
    "osiz",
    ziso_stream_open,
    ziso_stream_close,
    ziso_stream_get_size,
    ziso_stream_uncompress,
    ziso_stream_is_repeatable,
    ziso_stream_get_id,
    ziso_stream_free,
    ziso_update_size,
    ziso_get_input_stream,
    ziso_uncompress_cmp_ino,
    ziso_clone_stream
};


static
int ziso_cmp_ino(IsoStream *s1, IsoStream *s2)
{
    /* This function may rely on being called by iso_stream_cmp_ino()
       only with s1, s2 which both point to it as their .cmp_ino() function. 
       It would be a programming error to let any other than
       ziso_stream_compress_class point to ziso_cmp_ino().
    */
    if (s1->class != s2->class || (s1->class != &ziso_stream_compress_class &&
                                   s2->class != &ziso_stream_uncompress_class))
        iso_stream_cmp_ino(s1, s2, 1);

    /* Both streams apply the same treatment to their input streams */
    return iso_stream_cmp_ino(iso_stream_get_input_stream(s1, 0),
                              iso_stream_get_input_stream(s2, 0), 0);
}


static
int ziso_uncompress_cmp_ino(IsoStream *s1, IsoStream *s2)
{
    /* This function may rely on being called by iso_stream_cmp_ino()
       only with s1, s2 which both point to it as their .cmp_ino() function. 
       It would be a programming error to let any other than
       ziso_stream_uncompress_class point to ziso_uncompress_cmp_ino().
       This fallback endangers transitivity of iso_stream_cmp_ino().
    */
    if (s1->class != s2->class ||
        (s1->class != &ziso_stream_uncompress_class &&
         s2->class != &ziso_stream_uncompress_class))
        iso_stream_cmp_ino(s1, s2, 1);

    /* Both streams apply the same treatment to their input streams */
    return iso_stream_cmp_ino(iso_stream_get_input_stream(s1, 0),
                              iso_stream_get_input_stream(s2, 0), 0);
}


/* ------------------------------------------------------------------------- */



#ifdef Libisofs_with_zliB

static
void ziso_filter_free(FilterContext *filter)
{
    /* no data are allocated */;
}


/*
 * @param flag bit1= Install a decompression filter
 */
static
int ziso_filter_get_filter(FilterContext *filter, IsoStream *original, 
                           IsoStream **filtered, int flag)
{
    IsoStream *str;
    ZisofsFilterStreamData *data;
    ZisofsComprStreamData *cnstd = NULL;
    ZisofsUncomprStreamData *unstd = NULL;

    if (filter == NULL || original == NULL || filtered == NULL) {
        return ISO_NULL_POINTER;
    }

    str = calloc(sizeof(IsoStream), 1);
    if (str == NULL) {
        return ISO_OUT_OF_MEM;
    }
    if (flag & 2) {
        unstd = calloc(sizeof(ZisofsUncomprStreamData), 1);
        data = (ZisofsFilterStreamData *) unstd;
    } else {
        cnstd = calloc(sizeof(ZisofsComprStreamData), 1);
        data = (ZisofsFilterStreamData *) cnstd;
    }
    if (data == NULL) {
        free(str);
        return ISO_OUT_OF_MEM;
    }

    /* These data items are not owned by this filter object */
    data->id = ++ziso_ino_id;
    data->orig = original;
    data->size = -1;
    data->running = NULL;

    /* get reference to the source */
    iso_stream_ref(data->orig);

    str->refcount = 1;
    str->data = data;
    if (flag & 2) {
        unstd->zisofs_algo_num = 0;
        unstd->header_size_div4 = 0;
        unstd->block_size_log2 = 0;
        str->class = &ziso_stream_uncompress_class;
        ziso_osiz_ref_count++;
    } else {
        cnstd->orig_size = iso_stream_get_size(original);
        cnstd->block_pointers = NULL;
        cnstd->block_pointer_counter = 0;
        cnstd->open_counter = 0;
        cnstd->block_pointers_dropped = 0;
        str->class = &ziso_stream_compress_class;
        ziso_ref_count++;
    }

    *filtered = str;

    return ISO_SUCCESS;
}


/* To be called by iso_file_add_filter().
 * The FilterContext input parameter is not furtherly needed for the 
 * emerging IsoStream.
 */
static
int ziso_filter_get_compressor(FilterContext *filter, IsoStream *original, 
                               IsoStream **filtered)
{
    return ziso_filter_get_filter(filter, original, filtered, 0);
}

static
int ziso_filter_get_uncompressor(FilterContext *filter, IsoStream *original, 
                                 IsoStream **filtered)
{
    return ziso_filter_get_filter(filter, original, filtered, 2);
}


/* Produce a parameter object suitable for iso_file_add_filter().
 * It may be disposed by free() after all those calls are made.
 *
 * This is quite a dummy as it does not carry individual data.
 * @param flag bit1= Install a decompression filter
 */
static
int ziso_create_context(FilterContext **filter, int flag)
{
    FilterContext *f;
    
    *filter = f = calloc(1, sizeof(FilterContext));
    if (f == NULL) {
        return ISO_OUT_OF_MEM;
    }
    f->refcount = 1;
    f->version = 0;
    f->data = NULL;
    f->free = ziso_filter_free;
    if (flag & 2)
        f->get_filter = ziso_filter_get_uncompressor;
    else
        f->get_filter = ziso_filter_get_compressor;
    return ISO_SUCCESS;
}

#endif /* Libisofs_with_zliB */

/*
 * @param flag bit0= if_block_reduction rather than if_reduction
 *             bit1= Install a decompression filter
 *             bit2= only inquire availability of zisofs filtering
 *             bit3= do not inquire size
 */
int ziso_add_filter(IsoFile *file, int flag)
{

#ifdef Libisofs_with_zliB

    int ret;
    FilterContext *f = NULL;
    IsoStream *stream;
    off_t original_size = 0, filtered_size = 0;

    if (flag & 4)
        return 2;

    original_size = iso_file_get_size(file);
    if (!(flag & 2)) {
        if (original_size <= 0 || ((flag & 1) && original_size <= 2048)) {
            return 2;
        }
        if (original_size >= (off_t) ISO_ZISOFS_V1_LIMIT && !ziso_v2_enabled) {
            return ISO_ZISOFS_TOO_LARGE;
        }
    }

    ret = ziso_create_context(&f, flag & 2);
    if (ret < 0) {
        return ret;
    }
    ret = iso_file_add_filter(file, f, 0);
    free(f);
    if (ret < 0) {
        return ret;
    }
    if (flag & 8) /* size will be filled in by caller */
        return ISO_SUCCESS;

    /* Run a full filter process getsize so that the size is cached */
    stream = iso_file_get_stream(file);
    filtered_size = iso_stream_get_size(stream);
    if (filtered_size < 0) {
        iso_file_remove_filter(file, 0);
        return filtered_size;
    }
    if ((filtered_size >= original_size ||
        ((flag & 1) && filtered_size / 2048 >= original_size / 2048))
        && !(flag & 2)){
        ret = iso_file_remove_filter(file, 0);
        if (ret < 0) {
            return ret;
        }
        return 2;
    }
    return ISO_SUCCESS;

#else

    return ISO_ZLIB_NOT_ENABLED;

#endif /* ! Libisofs_with_zliB */

}


/* API function */
int iso_file_add_zisofs_filter(IsoFile *file, int flag)
{
    return ziso_add_filter(file, flag & ~8);
}


/* API function */
int iso_zisofs_get_refcounts(off_t *ziso_count, off_t *osiz_count, int flag)
{
    *ziso_count = ziso_ref_count;
    *osiz_count = ziso_osiz_ref_count;
    return ISO_SUCCESS;
}


int ziso_add_osiz_filter(IsoFile *file, uint8_t zisofs_algo[2],
                         uint8_t header_size_div4, uint8_t block_size_log2,
                         uint64_t uncompressed_size, int flag)
{   

#ifdef Libisofs_with_zliB

    int ret;
    ZisofsUncomprStreamData *unstd;

    ret = ziso_add_filter(file, 2 | 8);
    if (ret < 0)
        return ret;
    unstd = iso_file_get_stream(file)->data;
    ret = ziso_algo_to_num(zisofs_algo);
    if (ret < 0)
        return ISO_ZISOFS_WRONG_INPUT;
    unstd->zisofs_algo_num = ret;
    unstd->header_size_div4 = header_size_div4;
    unstd->block_size_log2 = block_size_log2;
    unstd->std.size = uncompressed_size;
    return ISO_SUCCESS;
    
#else

    return ISO_ZLIB_NOT_ENABLED;
    
#endif /* ! Libisofs_with_zliB */
    
}   



/* Determine stream type : 1=ziso , -1=osiz , 0=other , 2=ziso_by_content
   and eventual ZF field parameters
   @param flag bit0= allow ziso_by_content which is based on content reading
               bit1= do not inquire stream->class for filters
               bit2= recognize zisofs2 by magic only if ziso_v2_enabled
*/
int ziso_is_zisofs_stream(IsoStream *stream, int *stream_type,
                          uint8_t zisofs_algo[2],
                          int *header_size_div4, int *block_size_log2,
                          uint64_t *uncompressed_size, int flag)
{
    int ret, close_ret, algo_ret;
    ZisofsFilterStreamData *data;
    ZisofsComprStreamData *cnstd;
    ZisofsUncomprStreamData *unstd;
    uint8_t algo_num;

    *stream_type = 0; 
    if (stream->class == &ziso_stream_compress_class && !(flag & 2)) {
        *stream_type = 1;
        cnstd = stream->data;
        *uncompressed_size = cnstd->orig_size;
        *block_size_log2 = ziso_decide_bs_log2((off_t) *uncompressed_size);
        if (ziso_decide_v2_usage((off_t) *uncompressed_size)) {
          zisofs_algo[0] = 'P';
          zisofs_algo[1] = 'Z';
          *header_size_div4 = 6;
        } else if (*uncompressed_size < (uint64_t) ISO_ZISOFS_V1_LIMIT) {
          zisofs_algo[0] = 'p';
          zisofs_algo[1] = 'z';
          *header_size_div4 = 4;
        } else {
          return 0;
        }
        return 1;
    } else if(stream->class == &ziso_stream_uncompress_class && !(flag & 2)) {
        *stream_type = -1;
        data = stream->data;
        unstd = stream->data;
        ret = ziso_num_to_algo(unstd->zisofs_algo_num, zisofs_algo);
        if (ret < 0) 
            return ISO_ZISOFS_WRONG_INPUT;
        *header_size_div4 = unstd->header_size_div4;
        *block_size_log2 = unstd->block_size_log2;
        *uncompressed_size = data->size;
        return 1;
    }
    if (!(flag & 1))
        return 0;

    ret = iso_stream_open(stream);
    if (ret < 0) 
        return ret;
    ret = ziso_parse_zisofs_head(stream, &algo_num, header_size_div4,
                                 block_size_log2, uncompressed_size,
                                 (flag >> 2) & 1);
    if (ret == 1) {
        *stream_type = 2;
        algo_ret = ziso_num_to_algo(algo_num, zisofs_algo);
    } else {
        ret = 0;
        algo_ret = 1;
    }
    close_ret = iso_stream_close(stream);
    if (algo_ret < 0) 
        return ISO_ZISOFS_WRONG_INPUT;
    if (close_ret < 0) 
        return close_ret;

    return ret;
}


/* API */
int iso_zisofs_set_params(struct iso_zisofs_ctrl *params, int flag)
{

#ifdef Libisofs_with_zliB

    if (params->version < 0 || params->version > 1)
       return ISO_WRONG_ARG_VALUE;

    if (params->compression_level < 0 || params->compression_level > 9 ||
        params->block_size_log2 < ISO_ZISOFS_V1_MIN_LOG2 ||
        params->block_size_log2  > ISO_ZISOFS_V1_MAX_LOG2) {
        return ISO_WRONG_ARG_VALUE;
    }
    if (params->version >= 1)
        if (params->v2_enabled < 0 || params->v2_enabled > 2 ||
            (params->v2_block_size_log2 != 0 &&
             (params->v2_block_size_log2 < ISO_ZISOFS_V2_MIN_LOG2 ||
              params->v2_block_size_log2 > ISO_ZISOFS_V2_MAX_LOG2)))
            return ISO_WRONG_ARG_VALUE;
    if (ziso_ref_count > 0) {
        return ISO_ZISOFS_PARAM_LOCK;
    }
    ziso_compression_level = params->compression_level;
    ziso_block_size_log2 = params->block_size_log2;

    if (params->version == 0)
        return 1;

    ziso_v2_enabled = params->v2_enabled;
    if (params->v2_block_size_log2 > 0)
        ziso_v2_block_size_log2 = params->v2_block_size_log2;
    if (params->max_total_blocks > 0)
        ziso_max_total_blocks = params->max_total_blocks;
    if (params->max_file_blocks > 0)
        ziso_max_file_blocks = params->max_file_blocks;
    if (params->block_number_target != 0)
        ziso_block_number_target = params->block_number_target;
    if (params->bpt_discard_file_blocks != 0)
        ziso_many_block_limit = params->bpt_discard_file_blocks;
    if (params->bpt_discard_free_ratio != 0.0)
        ziso_keep_blocks_free_ratio = params->bpt_discard_free_ratio;

    return 1;
    
#else

    return ISO_ZLIB_NOT_ENABLED;
    
#endif /* ! Libisofs_with_zliB */
    
}


/* API */
int iso_zisofs_get_params(struct iso_zisofs_ctrl *params, int flag)
{

#ifdef Libisofs_with_zliB

    if (params->version < 0 || params->version > 1)
       return ISO_WRONG_ARG_VALUE;

    params->compression_level = ziso_compression_level;
    params->block_size_log2 = ziso_block_size_log2;
    if (params->version == 1) {
        params->v2_enabled = ziso_v2_enabled;
        params->v2_block_size_log2 = ziso_v2_block_size_log2;
        params->max_total_blocks = ziso_max_total_blocks;
        params->current_total_blocks = ziso_block_pointer_mgt((uint64_t) 0, 3);
        params->max_file_blocks = ziso_max_file_blocks;
        params->block_number_target = ziso_block_number_target;
        params->bpt_discard_file_blocks = ziso_many_block_limit;
        params->bpt_discard_free_ratio = ziso_keep_blocks_free_ratio;
    }
    return 1;

#else

    return ISO_ZLIB_NOT_ENABLED;
    
#endif /* ! Libisofs_with_zliB */
    
}


/* API */
int iso_stream_get_zisofs_par(IsoStream *stream, int *stream_type,
                              uint8_t zisofs_algo[2], uint8_t* algo_num,
                              int *block_size_log2, int flag)
{

#ifdef Libisofs_with_zliB

    uint64_t uncompressed_size;
    int header_size_div4, ret;
    
    if (stream == NULL)
        return ISO_NULL_POINTER;
    ret = ziso_is_zisofs_stream(stream, stream_type, zisofs_algo,
                                &header_size_div4, block_size_log2,
                                &uncompressed_size, 0);
    if (ret <= 0 || (*stream_type != -1 && *stream_type != 1))
        return 0;
    *algo_num = ziso_algo_to_num(zisofs_algo);
    return 1;

#else

    return ISO_ZLIB_NOT_ENABLED;
    
#endif /* ! Libisofs_with_zliB */

}


/* API */
int iso_stream_zisofs_discard_bpt(IsoStream *stream, int flag)
{

#ifdef Libisofs_with_zliB

    int ret;

    if (stream == NULL)
        return ISO_NULL_POINTER;
    ret = ziso_discard_bpt(stream, 1);
    return ret;

#else

    return ISO_ZLIB_NOT_ENABLED;
    
#endif /* ! Libisofs_with_zliB */

}


/* API */
int iso_zisofs_ctrl_susp_z2(int enable)
{
    if (enable == 0 || enable == 1)
        iso_zisofs2_enable_susp_z2 = enable;
    return iso_zisofs2_enable_susp_z2;
}
