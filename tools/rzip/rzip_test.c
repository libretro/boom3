#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "rzip.h"
static int warns = 0;
static void warn(const char *fmt, ...) { va_list a; va_start(a,fmt); vfprintf(stderr,"WARN: ",a); vfprintf(stderr,fmt,a); fprintf(stderr,"\n"); va_end(a); warns++; }
static unsigned char *slurp(const char *p, long *n){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);unsigned char*b=malloc(*n);fread(b,1,*n,f);fclose(f);return b;}
int main(void) {
    rzip_set_warn(warn);
    rzip_t *z = rzip_open("/tmp/test.pk4");
    if (!z) { printf("FAIL open\n"); return 1; }
    printf("entries: %d\n", rzip_num_entries(z));
    long n1, n2; unsigned char *d1 = slurp("/tmp/e1.bin",&n1), *d2 = slurp("/tmp/e2.bin",&n2);
    int fail = 0;
    for (int i = 0; i < rzip_num_entries(z); i++) {
        const rzip_entry_t *e = rzip_entry_at(z,i);
        printf("  [%d] %s method=%u usize=%llu csize=%llu\n", i, e->name, e->method,
            (unsigned long long)e->uncompressedSize,(unsigned long long)e->compressedSize);
        unsigned char *ref = NULL; long rn = 0;
        if (strstr(e->name,"stored")) { ref=d1; rn=n1; } else if (strstr(e->name,"deflated")) { ref=d2; rn=n2; }
        /* full single read */
        rzip_file_t *f = rzip_file_open(z,i);
        unsigned char *buf = malloc(rn?rn:1);
        int got = rzip_file_read(f, buf, (int)rn);
        if (got != rn || (rn && memcmp(buf,ref,rn))) { printf("FAIL full read %s (%d/%ld)\n",e->name,got,rn); fail++; }
        /* rewind + odd-chunk reads */
        if (rzip_file_rewind(f)) { printf("FAIL rewind\n"); fail++; }
        long pos=0; int sizes[]={1,7,4093,65536,13};int st=0;
        while (pos<rn){int c=sizes[st++%5]; if(pos+c>rn)c=(int)(rn-pos);
            int g=rzip_file_read(f,buf+pos,c); if(g!=c){printf("FAIL chunk %s @%ld\n",e->name,pos);fail++;break;} pos+=g;}
        if (rn && memcmp(buf,ref,rn)) { printf("FAIL chunked content %s\n",e->name); fail++; }
        /* Seek pattern: rewind + skip to 3/4, read tail */
        if (rn > 100) {
            rzip_file_rewind(f);
            long skip = rn*3/4; char tmp[32768]; long sp=0;
            while (sp<skip){int c=(int)((skip-sp)>32768?32768:(skip-sp)); if(rzip_file_read(f,tmp,c)!=c){printf("FAIL skip\n");fail++;break;} sp+=c;}
            int g=rzip_file_read(f,buf,(int)(rn-skip));
            if (g!=(int)(rn-skip)||memcmp(buf,ref+skip,rn-skip)){printf("FAIL tail %s\n",e->name);fail++;}
        }
        rzip_file_close(f); free(buf);
    }
    if (warns) { printf("FAIL unexpected warnings clean pak\n"); fail++; }
    rzip_close(z);
    /* corruption: flip a byte mid-file, expect CRC warning on full read */
    { long zn; unsigned char *zb=slurp("/tmp/test.pk4",&zn); zb[zn/3]^=0xFF;
      FILE*o=fopen("/tmp/bad.pk4","wb");fwrite(zb,1,zn,o);fclose(o);
      rzip_t *b=rzip_open("/tmp/bad.pk4");
      if (b){ int w0=warns;
        for(int i=0;i<rzip_num_entries(b);i++){const rzip_entry_t*e=rzip_entry_at(b,i);
          rzip_file_t*f=rzip_file_open(b,i); if(!f)continue;
          unsigned char*bb=malloc(e->uncompressedSize?e->uncompressedSize:1);
          rzip_file_read(f,bb,(int)e->uncompressedSize); rzip_file_close(f); free(bb);}
        printf("corruption detected: %s\n", warns>w0?"yes":"NO - FAIL"); if(warns==w0)fail++;
        rzip_close(b);
      } else printf("corruption: open refused (acceptable)\n");
    }
    printf(fail?"RESULT: FAIL(%d)\n":"RESULT: ALL OK\n", fail);
    return fail?1:0;
}
