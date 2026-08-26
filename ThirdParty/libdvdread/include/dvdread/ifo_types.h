#ifndef LIBDVDREAD_IFO_TYPES_H
#define LIBDVDREAD_IFO_TYPES_H

#include <stdint.h>
#include "dvd_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t frame_u;
} dvd_time_t;

typedef struct {
    uint8_t mpeg_version : 2;
    uint8_t video_format : 2;
    uint8_t display_aspect_ratio : 2;
    uint8_t permitted_df : 2;
    uint8_t line21_cc_1 : 1;
    uint8_t line21_cc_2 : 1;
    uint8_t letterboxed : 1;
    uint8_t film_mode : 1;
    uint8_t video_resolution : 2;
    uint8_t zero_1 : 2;
    uint8_t picture_size : 2;
    uint8_t bit_rate : 1;
    uint8_t unregistered : 1;
} video_attr_t;

typedef struct {
    uint8_t audio_format : 3;
    uint8_t multich_extension : 1;
    uint8_t lang_type : 2;
    uint8_t application_mode : 2;
    uint8_t quantization : 2;
    uint8_t sample_frequency : 2;
    uint8_t zero_1 : 1;
    uint8_t channels : 3;
    uint16_t lang_code;
    uint8_t lang_extension;
    uint8_t code_extension;
    uint8_t unknown3;
    uint8_t unknown4;
} audio_attr_t;

typedef struct {
    uint8_t code_mode : 3;
    uint8_t zero_1 : 3;
    uint8_t type : 2;
    uint8_t zero_2;
    uint16_t lang_code;
    uint8_t lang_extension;
    uint8_t code_extension;
} subp_attr_t;

typedef struct {
    uint8_t pb_ty;
    uint8_t nr_of_angles;
    uint16_t nr_of_ptts;
    uint16_t parental_id;
    uint8_t title_set_nr;
    uint8_t vts_ttn;
    uint32_t title_start_sector;
} title_info_t;

typedef struct {
    uint16_t nr_of_srpts;
    uint16_t zero_1;
    uint32_t last_byte;
    title_info_t *title;
} tt_srpt_t;

typedef struct {
    uint16_t pgcn;
    uint16_t pgn;
} ptt_info_t;

typedef struct {
    uint16_t nr_of_ptts;
    ptt_info_t *ptt;
} ttu_t;

typedef struct {
    uint16_t nr_of_srpts;
    uint16_t zero_1;
    uint32_t last_byte;
    ttu_t *title;
} vts_ptt_srpt_t;

typedef struct {
    uint16_t block_mode : 2;
    uint16_t block_type : 2;
    uint16_t seamless_play : 1;
    uint16_t interleaved : 1;
    uint16_t stc_spu_reset : 1;
    uint16_t seamless_angle : 1;
    uint16_t zero_1 : 8;
    uint8_t playback_mode;
    uint8_t rest;
    dvd_time_t playback_time;
    uint32_t first_sector;
    uint32_t first_ilvu_end_sector;
    uint32_t last_vobu_start_sector;
    uint32_t last_sector;
} cell_playback_t;

typedef struct {
    uint16_t vob_id_nr;
    uint8_t zero_1;
    uint8_t cell_nr;
} cell_position_t;

typedef struct {
    uint16_t zero_1;
    uint8_t nr_of_programs;
    uint8_t nr_of_cells;
    dvd_time_t playback_time;
    uint32_t prohibited_ops;
    uint16_t audio_control[8];
    uint32_t subp_control[32];
    uint16_t next_pgc_nr;
    uint16_t prev_pgc_nr;
    uint16_t goup_pgc_nr;
    uint8_t pg_playback_mode;
    uint8_t still_time;
    uint32_t clut[16];
    uint16_t command_tbl_offset;
    uint16_t program_map_offset;
    uint16_t cell_playback_offset;
    uint16_t cell_position_offset;
    void *command_tbl;
    uint8_t *program_map;
    cell_playback_t *cell_playback;
    cell_position_t *cell_position;
} pgc_t;

typedef struct {
    uint8_t entry_id;
    uint8_t block_mode : 2;
    uint8_t block_type : 2;
    uint8_t zero_1 : 4;
    uint16_t ptl_id_mask;
    uint32_t pgc_start_byte;
    pgc_t *pgc;
} pgci_srp_t;

typedef struct {
    uint16_t nr_of_pgci_srp;
    uint16_t zero_1;
    uint32_t last_byte;
    pgci_srp_t *pgci_srp;
} pgcit_t;

typedef struct {
    char vmg_identifier[12];
    uint32_t vmg_last_sector;
    uint8_t zero_1[12];
    uint32_t vmgi_last_sector;
    uint8_t zero_2;
    uint8_t dvd_version;
    uint32_t vmg_category;
    uint16_t vmg_nr_of_volumes;
    uint16_t vmg_this_volume_nr;
    uint8_t disc_size;
    uint8_t zero_3[19];
    uint32_t vmgm_vobs;
    uint32_t tt_srpt;
    uint32_t vmgm_pgci_ut;
    uint32_t ptl_mait;
    uint32_t vts_atrt;
    uint32_t txtdt_mgi;
    uint32_t vmgm_c_adt;
    uint32_t vmgm_vobu_admap;
    uint8_t zero_4[32];
    video_attr_t vmgm_video_attr;
    uint8_t nr_of_vmgm_audio_streams;
    audio_attr_t vmgm_audio_attr;
    uint8_t zero_5[16];
    uint8_t nr_of_vmgm_subp_streams;
    subp_attr_t vmgm_subp_attr;
} vmgi_mat_t;

typedef struct {
    char vts_identifier[12];
    uint32_t vts_last_sector;
    uint8_t zero_1[12];
    uint32_t vtsi_last_sector;
    uint8_t zero_2;
    uint8_t dvd_version;
    uint32_t vts_category;
    uint8_t zero_3[90];
    uint32_t vtsm_vobs;
    uint32_t vtstt_vobs;
    uint32_t vts_ptt_srpt;
    uint32_t vts_pgcit;
    uint32_t vtsm_pgci_ut;
    uint32_t vts_tmapt;
    uint32_t vtsm_c_adt;
    uint32_t vtsm_vobu_admap;
    uint32_t vts_c_adt;
    uint32_t vts_vobu_admap;
    uint8_t zero_4[24];
    video_attr_t vtsm_video_attr;
    uint8_t nr_of_vtsm_audio_streams;
    audio_attr_t vtsm_audio_attr;
    uint8_t zero_5[16];
    uint8_t nr_of_vtsm_subp_streams;
    subp_attr_t vtsm_subp_attr;
    uint8_t zero_6[2];
    video_attr_t vts_video_attr;
    uint8_t nr_of_vts_audio_streams;
    audio_attr_t vts_audio_attr[8];
    uint8_t zero_7[16];
    uint8_t nr_of_vts_subp_streams;
    subp_attr_t vts_subp_attr[32];
} vtsi_mat_t;

typedef struct {
    dvd_reader_t *dvd;
    dvd_file_t *dvd_file;
    vmgi_mat_t *vmgi_mat;
    tt_srpt_t *tt_srpt;
    void *first_play_pgc;
    void *vmgm_pgci_ut;
    void *ptl_mait;
    void *vts_atrt;
    void *txtdt_mgi;
    void *vmgm_c_adt;
    void *vmgm_vobu_admap;
    vtsi_mat_t *vtsi_mat;
    vts_ptt_srpt_t *vts_ptt_srpt;
    pgcit_t *vts_pgcit;
    void *vtsm_pgci_ut;
    void *vts_tmapt;
    void *vtsm_c_adt;
    void *vtsm_vobu_admap;
    void *vts_c_adt;
    void *vts_vobu_admap;
} ifo_handle_t;

#ifdef __cplusplus
}
#endif

#endif /* LIBDVDREAD_IFO_TYPES_H */
