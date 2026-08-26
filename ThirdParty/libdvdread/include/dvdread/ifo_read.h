#ifndef LIBDVDREAD_IFO_READ_H
#define LIBDVDREAD_IFO_READ_H

#include "ifo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

ifo_handle_t *ifoOpen(dvd_reader_t *dvd, int title);
ifo_handle_t *ifoOpenVMGI(dvd_reader_t *dvd);
ifo_handle_t *ifoOpenVTSI(dvd_reader_t *dvd, int title_set_nr);
void ifoClose(ifo_handle_t *ifohandle);

int ifoRead_FP_PGC(ifo_handle_t *ifohandle);
int ifoRead_TT_SRPT(ifo_handle_t *ifohandle);
int ifoRead_PGCI_UT(ifo_handle_t *ifohandle);
int ifoRead_PTL_MAIT(ifo_handle_t *ifohandle);
int ifoRead_VTS_ATRT(ifo_handle_t *ifohandle);
int ifoRead_TXTDT_MGI(ifo_handle_t *ifohandle);
int ifoRead_C_ADT(ifo_handle_t *ifohandle);
int ifoRead_VOBU_ADMAP(ifo_handle_t *ifohandle);
int ifoRead_TITLE_C_ADT(ifo_handle_t *ifohandle);
int ifoRead_TITLE_VOBU_ADMAP(ifo_handle_t *ifohandle);
int ifoRead_VTS_PTT_SRPT(ifo_handle_t *ifohandle);
int ifoRead_PGCIT(ifo_handle_t *ifohandle);
int ifoRead_VTS_TMAPT(ifo_handle_t *ifohandle);

#ifdef __cplusplus
}
#endif

#endif /* LIBDVDREAD_IFO_READ_H */
