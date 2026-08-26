#ifndef LIBDVDREAD_DVD_READER_H
#define LIBDVDREAD_DVD_READER_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DVD_VIDEO_LB_LEN 2048

typedef struct dvd_reader_s dvd_reader_t;
typedef struct dvd_file_s dvd_file_t;

typedef enum {
    DVD_READ_INFO_FILE,
    DVD_READ_INFO_BACKUP_FILE,
    DVD_READ_MENU_VOBS,
    DVD_READ_TITLE_VOBS
} dvd_read_domain_t;

typedef struct {
    off_t size;
    int nr_parts;
    off_t parts_size[9];
} dvd_stat_t;

dvd_reader_t *DVDOpen(const char *path);
void DVDClose(dvd_reader_t *dvd);

dvd_file_t *DVDOpenFile(dvd_reader_t *dvd, int titleno, dvd_read_domain_t domain);
void DVDCloseFile(dvd_file_t *dvd_file);

ssize_t DVDReadBlocks(dvd_file_t *dvd_file, int offset, size_t block_count, unsigned char *data);
ssize_t DVDReadBytes(dvd_file_t *dvd_file, void *data, size_t byte_count);
int DVDFileSeek(dvd_file_t *dvd_file, int offset);
ssize_t DVDFileSize(dvd_file_t *dvd_file);
int DVDFileStat(dvd_reader_t *dvd, int titleno, dvd_read_domain_t domain, dvd_stat_t *statbuf);

int DVDISOVolumeInfo(dvd_reader_t *dvd, char *volid, unsigned int volid_size,
                     unsigned char *volsetid, unsigned int volsetid_size);
int DVDUDFVolumeInfo(dvd_reader_t *dvd, char *volid, unsigned int volid_size,
                     unsigned char *volsetid, unsigned int volsetid_size);

#ifdef __cplusplus
}
#endif

#endif /* LIBDVDREAD_DVD_READER_H */
