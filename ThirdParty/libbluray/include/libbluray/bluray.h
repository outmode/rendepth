/*
 * This file is part of libbluray
 * Copyright (C) 2009-2010  John Stebbins
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _BLURAY_H_
#define _BLURAY_H_

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TITLES_ALL                0
#define TITLES_FILTER_DUP_TITLE   0x01
#define TITLES_FILTER_DUP_CLIP    0x02
#define TITLES_RELEVANT           (TITLES_FILTER_DUP_TITLE | TITLES_FILTER_DUP_CLIP)

#define BLURAY_STREAM_TYPE_VIDEO_MPEG1     0x01
#define BLURAY_STREAM_TYPE_VIDEO_MPEG2     0x02
#define BLURAY_STREAM_TYPE_AUDIO_MPEG1     0x03
#define BLURAY_STREAM_TYPE_AUDIO_MPEG2     0x04
#define BLURAY_STREAM_TYPE_AUDIO_LPCM      0x80
#define BLURAY_STREAM_TYPE_AUDIO_AC3       0x81
#define BLURAY_STREAM_TYPE_AUDIO_DTS       0x82
#define BLURAY_STREAM_TYPE_AUDIO_TRUHD     0x83
#define BLURAY_STREAM_TYPE_AUDIO_AC3PLUS   0x84
#define BLURAY_STREAM_TYPE_AUDIO_DTSHD     0x85
#define BLURAY_STREAM_TYPE_AUDIO_DTSHD_MASTER 0x86
#define BLURAY_STREAM_TYPE_VIDEO_VC1       0xea
#define BLURAY_STREAM_TYPE_VIDEO_H264      0x1b
#define BLURAY_STREAM_TYPE_VIDEO_HEVC      0x24
#define BLURAY_STREAM_TYPE_SUB_PG          0x90
#define BLURAY_STREAM_TYPE_SUB_IG          0x91
#define BLURAY_STREAM_TYPE_SUB_TEXT        0x92

#define BLURAY_VIDEO_FORMAT_480I           1
#define BLURAY_VIDEO_FORMAT_576I           2
#define BLURAY_VIDEO_FORMAT_480P           3
#define BLURAY_VIDEO_FORMAT_1080I          4
#define BLURAY_VIDEO_FORMAT_720P           5
#define BLURAY_VIDEO_FORMAT_1080P          6
#define BLURAY_VIDEO_FORMAT_576P           7
#define BLURAY_VIDEO_FORMAT_2160P          8

#define BLURAY_VIDEO_RATE_24000_1001       1
#define BLURAY_VIDEO_RATE_24               2
#define BLURAY_VIDEO_RATE_25               3
#define BLURAY_VIDEO_RATE_30000_1001       4
#define BLURAY_VIDEO_RATE_50               6
#define BLURAY_VIDEO_RATE_60000_1001       7

#define BLURAY_VIDEO_ASPECT_4_3            2
#define BLURAY_VIDEO_ASPECT_16_9           3

#define BLURAY_AUDIO_FORMAT_MONO           1
#define BLURAY_AUDIO_FORMAT_STEREO         3
#define BLURAY_AUDIO_FORMAT_MULTI_CHANNEL  6
#define BLURAY_AUDIO_FORMAT_COMBO          12

#define BLURAY_AUDIO_RATE_48               1
#define BLURAY_AUDIO_RATE_96               4
#define BLURAY_AUDIO_RATE_192              5
#define BLURAY_AUDIO_RATE_192_COMBO        12
#define BLURAY_AUDIO_RATE_96_COMBO         14

#define BLURAY_TITLE_TYPE_HDMV             1
#define BLURAY_TITLE_TYPE_BDJ              2

typedef struct bd_disc_s BLURAY;

typedef struct bd_title_mark {
    uint8_t  idx;
    uint8_t  type;
    uint16_t number;
    uint64_t start;
    uint32_t duration;
    uint32_t offset;
    uint32_t clip_ref;
} BLURAY_TITLE_MARK;

typedef struct bd_stream_info {
    uint8_t  coding_type;
    uint8_t  format;
    uint8_t  rate;
    uint8_t  char_code;
    uint8_t  lang[4];
    uint16_t pid;
    uint8_t  aspect;
    uint8_t  subpath_id;
} BLURAY_STREAM_INFO;

typedef struct bd_clip_info {
    uint32_t clip_id;
    uint64_t in_time;
    uint64_t out_time;
    uint8_t  video_stream_count;
    uint8_t  audio_stream_count;
    uint8_t  pg_stream_count;
    uint8_t  ig_stream_count;
    uint8_t  sec_audio_stream_count;
    uint8_t  sec_video_stream_count;
    BLURAY_STREAM_INFO *video_streams;
    BLURAY_STREAM_INFO *audio_streams;
    BLURAY_STREAM_INFO *pg_streams;
    BLURAY_STREAM_INFO *ig_streams;
    BLURAY_STREAM_INFO *sec_audio_streams;
    BLURAY_STREAM_INFO *sec_video_streams;
} BLURAY_CLIP_INFO;

typedef struct bd_title_info {
    uint32_t idx;
    uint32_t playlist;
    uint64_t duration;
    uint32_t clip_count;
    uint32_t angle_count;
    uint8_t  chapter_count;
    uint8_t  mark_count;
    BLURAY_TITLE_MARK *marks;
    BLURAY_CLIP_INFO  *clips;
    uint8_t  title_type;
    uint8_t  accessible;
    uint8_t  mvc_base_view_r;
} BLURAY_TITLE_INFO;

typedef struct bd_disc_info {
    uint8_t bluray_detected;
    uint8_t disc_name[33];
    uint8_t udf_volume_id[33];
    uint8_t bdj_detected;
    uint8_t bdj_supported;
    uint8_t bdj_handled;
    uint8_t bdjo_detected;
    uint8_t aacs_detected;
    uint8_t libaacs_detected;
    uint8_t aacs_handled;
    uint8_t bdplus_detected;
    uint8_t libbdplus_detected;
    uint8_t bdplus_handled;
    uint8_t disc_id[20];
    uint8_t num_titles;
    uint8_t num_hdmv_titles;
    uint8_t num_bdj_titles;
    uint8_t num_unsupported_titles;
    uint8_t first_play_supported;
    uint8_t top_menu_supported;
    uint8_t no_menu_support;
    uint8_t aacs_error_code;
    uint8_t bdplus_error_code;
    uint8_t bdmv_version;
} BLURAY_DISC_INFO;

BLURAY *bd_open(const char *device_path, const char *keyfile_path);
void bd_close(BLURAY *bd);

uint32_t bd_get_titles(BLURAY *bd, uint8_t flags, uint32_t min_title_length);
BLURAY_TITLE_INFO *bd_get_title_info(BLURAY *bd, uint32_t title_idx, unsigned angle);
BLURAY_TITLE_INFO *bd_get_playlist_info(BLURAY *bd, uint32_t playlist, unsigned angle);
void bd_free_title_info(BLURAY_TITLE_INFO *title_info);

const BLURAY_DISC_INFO *bd_get_disc_info(BLURAY *bd);

int bd_select_title(BLURAY *bd, uint32_t title_idx);
int bd_select_playlist(BLURAY *bd, uint32_t playlist);
int bd_select_angle(BLURAY *bd, unsigned angle);

int64_t bd_seek(BLURAY *bd, uint64_t pos);
int64_t bd_seek_time(BLURAY *bd, uint64_t tick);
int64_t bd_seek_chapter(BLURAY *bd, unsigned chapter);
int64_t bd_seek_mark(BLURAY *bd, unsigned mark);

int64_t bd_tell(BLURAY *bd);
int64_t bd_tell_time(BLURAY *bd);

uint32_t bd_get_current_title(BLURAY *bd);
uint32_t bd_get_current_angle(BLURAY *bd);
uint32_t bd_get_current_chapter(BLURAY *bd);

uint64_t bd_get_title_size(BLURAY *bd);

int bd_read(BLURAY *bd, unsigned char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* _BLURAY_H_ */
