/* OSES öbeği: verilen tek alan içinde bölen/birleştiren ayırıcı. */
#ifndef OSES_YIGIN_H
#define OSES_YIGIN_H
#include <stdint.h>
struct ob_blok;
typedef struct {
    uint8_t *bas, *son;
    struct ob_blok *bos;
    uint64_t kullanilan, tepe;
} ob_yigin_t;
int   ob_kur(ob_yigin_t *y, void *alan, uint64_t boy);   /* 0 / -1 */
void *ob_ayir(ob_yigin_t *y, uint64_t n);                 /* 16 bayt hizalı, yoksa 0 */
void  ob_birak(ob_yigin_t *y, void *p);
#endif
