#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <libretro.h>
#include <streams/file_stream.h>
#include <vfs/vfs_implementation.h>
#include <vfs/vfs_hybrid.h>
#include "rzip.h"
static int warns = 0;
static void warn(const char *fmt, ...) { va_list a; va_start(a,fmt); vfprintf(stderr,"WARN: ",a); vfprintf(stderr,fmt,a); fprintf(stderr,"\n"); va_end(a); warns++; }
static unsigned char *slurp(const char *p, long *n){FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);unsigned char*b=malloc(*n);if(fread(b,1,*n,f)!=(size_t)*n)*n=0;fclose(f);return b;}

/* A frontend that serves "fake://<path>" from the local file, at v5 or
 * at v6, counting its opens and positional reads: on v6 a pak's entries
 * read at offsets through the pak's own handle; on v5 each opens one. */
static unsigned fe_version;
static int fe_opens, fe_read_ats;

static const char *fe_path(const char *p) { return strncmp(p, "fake://", 7) ? p : p + 7; }
static const char *fe_get_path(struct retro_vfs_file_handle *s) { return retro_vfs_file_get_path_impl((libretro_vfs_implementation_file*)s); }
static struct retro_vfs_file_handle *fe_open(const char *p, unsigned m, unsigned h)
{ fe_opens++; return (struct retro_vfs_file_handle*)retro_vfs_file_open_impl(fe_path(p), m, h & ~RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS); }
static int fe_close(struct retro_vfs_file_handle *s) { return retro_vfs_file_close_impl((libretro_vfs_implementation_file*)s); }
static int64_t fe_size(struct retro_vfs_file_handle *s) { return retro_vfs_file_size_impl((libretro_vfs_implementation_file*)s); }
static int64_t fe_tell(struct retro_vfs_file_handle *s) { return retro_vfs_file_tell_impl((libretro_vfs_implementation_file*)s); }
static int64_t fe_seek(struct retro_vfs_file_handle *s, int64_t o, int w) { return retro_vfs_file_seek_impl((libretro_vfs_implementation_file*)s, o, w); }
static int64_t fe_read(struct retro_vfs_file_handle *s, void *d, uint64_t l) { return retro_vfs_file_read_impl((libretro_vfs_implementation_file*)s, d, l); }
static int64_t fe_write(struct retro_vfs_file_handle *s, const void *d, uint64_t l) { return retro_vfs_file_write_impl((libretro_vfs_implementation_file*)s, d, l); }
static int fe_flush(struct retro_vfs_file_handle *s) { return retro_vfs_file_flush_impl((libretro_vfs_implementation_file*)s); }
static int fe_remove(const char *p) { return retro_vfs_file_remove_impl(fe_path(p)); }
static int fe_rename(const char *a, const char *b) { return retro_vfs_file_rename_impl(fe_path(a), fe_path(b)); }
static int64_t fe_read_at(struct retro_vfs_file_handle *s, void *d, uint64_t l, int64_t o)
{ fe_read_ats++; return retro_vfs_file_read_at_impl((libretro_vfs_implementation_file*)s, d, l, o); }

static struct retro_vfs_interface fe_iface;

static bool env_cb(unsigned cmd, void *data)
{
    struct retro_vfs_interface_info *info = (struct retro_vfs_interface_info*)data;
    if (cmd != RETRO_ENVIRONMENT_GET_VFS_INTERFACE || info->required_interface_version > fe_version)
        return false;
    memset(&fe_iface, 0, sizeof(fe_iface));
    fe_iface.get_path = fe_get_path; fe_iface.open = fe_open; fe_iface.close = fe_close;
    fe_iface.size = fe_size; fe_iface.tell = fe_tell; fe_iface.seek = fe_seek;
    fe_iface.read = fe_read; fe_iface.write = fe_write; fe_iface.flush = fe_flush;
    fe_iface.remove = fe_remove; fe_iface.rename = fe_rename;
    if (fe_version >= 6)
        fe_iface.read_at = fe_read_at;
    info->required_interface_version = fe_version;
    info->iface = &fe_iface;
    return true;
}

static int check_pak(const char *path, int mapped_expected, const unsigned char *d1, long n1, const unsigned char *d2, long n2)
{
    int fail = 0, i;
    rzip_t *z = rzip_open(path);
    if (!z) { printf("FAIL open %s\n", path); return 1; }
    printf("%s: entries: %d\n", path, rzip_num_entries(z));
    for (i = 0; i < rzip_num_entries(z); i++) {
        const rzip_entry_t *e = rzip_entry_at(z,i);
        printf("  [%d] %s method=%u usize=%llu csize=%llu\n", i, e->name, e->method,
            (unsigned long long)e->uncompressedSize,(unsigned long long)e->compressedSize);
        const unsigned char *ref = NULL; long rn = 0;
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
    /* borrow contract: stored entries hand back the exact file bytes on
       mapped builds; deflated entries always NULL; on unmapped builds
       everything is NULL. Both via entry index and via an open handle. */
    for (i = 0; i < rzip_num_entries(z); i++) {
        const rzip_entry_t *e = rzip_entry_at(z,i);
        uint64_t bl = 0;
        const uint8_t *b = rzip_entry_borrow(z,i,&bl);
        rzip_file_t *fh = rzip_file_open(z,i);
        uint64_t bl2 = 0;
        const uint8_t *b2 = fh ? rzip_file_borrow(fh,&bl2) : NULL;
        if (e->method != 0 || !mapped_expected) {
            if (b || b2) { printf("FAIL borrow should be NULL for %s\n", e->name); fail++; }
        } else {
            const unsigned char *ref=NULL; long rn=0;
            if (strstr(e->name,"stored")) { ref=d1; rn=n1; }
            if (!b || !b2 || bl != e->uncompressedSize || bl2 != bl || b != b2) {
                printf("FAIL borrow shape for %s\n", e->name); fail++;
            } else if (ref && (long)bl==rn && memcmp(b,ref,rn)) {
                printf("FAIL borrow content %s\n", e->name); fail++;
            }
        }
        if (fh) rzip_file_close(fh);
    }
    printf("borrow contract (%s): checked\n", mapped_expected?"mapped":"unmapped");
    rzip_close(z);
    return fail;
}

int main(void) {
    rzip_set_warn(warn);
    long n1, n2; unsigned char *d1 = slurp("/tmp/e1.bin",&n1), *d2 = slurp("/tmp/e2.bin",&n2);
    int fail = 0;
    int mapped_expected =
#ifdef HAVE_MMAP
        1;
#else
        0;
#endif
    fail += check_pak("/tmp/test.pk4", mapped_expected, d1, n1, d2, n2);

    /* corruption: flip a byte mid-file, expect CRC warning on full read */
    { long zn; unsigned char *zb=slurp("/tmp/test.pk4",&zn); zb[zn/3]^=0xFF;
      FILE*o=fopen("/tmp/bad.pk4","wb");fwrite(zb,1,zn,o);fclose(o);
      free(zb);
      rzip_t *b=rzip_open("/tmp/bad.pk4");
      if (b){ int w0=warns;
        for(int i=0;i<rzip_num_entries(b);i++){const rzip_entry_t*e=rzip_entry_at(b,i);
          rzip_file_t*f=rzip_file_open(b,i); if(!f)continue;
          unsigned char*bb=malloc(e->uncompressedSize?e->uncompressedSize:1);
          rzip_file_read(f,bb,(int)e->uncompressedSize); rzip_file_close(f); free(bb);}
        printf("corruption detected: %s\n", warns>w0?"yes":"NO - FAIL"); if(warns==w0)fail++;
        rzip_close(b);
      } else printf("corruption: open refused (acceptable)\n");
      warns = 0;
    }

    /* through a frontend: never mapped; v6 reads entries at offsets on
       the pak's handle, v5 opens a handle per entry */
    {
        int v;
        for (v = 5; v <= 6; v++) {
            int opens;
            fe_version = (unsigned)v;
            fe_opens = fe_read_ats = 0;
            vfs_hybrid_init(env_cb, NULL);
            fail += check_pak("fake:///tmp/test.pk4", 0, d1, n1, d2, n2);
            opens = fe_opens;
            if (v == 6 && (opens != 1 || fe_read_ats == 0)) {
                printf("FAIL v6: %d opens, %d reads at offsets (want 1, some)\n", opens, fe_read_ats); fail++;
            } else if (v == 5 && (opens <= 1 || fe_read_ats != 0)) {
                printf("FAIL v5: %d opens, %d reads at offsets (want one per entry, none)\n", opens, fe_read_ats); fail++;
            } else
                printf("frontend v%d: %d opens, %d reads at offsets\n", v, opens, fe_read_ats);
        }
    }
    free(d1); free(d2);   /* harness hygiene: leak-clean under LSan */
    rzip_drain_pool();
    printf(fail?"RESULT: FAIL(%d)\n":"RESULT: ALL OK\n", fail);
    return fail?1:0;
}
