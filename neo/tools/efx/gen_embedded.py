#!/usr/bin/env python3
"""Regenerate neo/sound/efx_embedded.h from a directory of .efx files.
Bytes are embedded verbatim, CRLF included: the bit-exactness test
compares the arrays against the source files byte for byte.
Usage: gen_embedded.py <efxs-dir> <output-header>"""
import glob, os, sys

def main(srcdir, outpath):
    out=['/* Generated from the RoE efxs/ set - one entry per map, bytes verbatim',
    ' * including the CRLF line endings, because the bit-exactness test',
    ' * compares these arrays against the source files byte for byte before',
    ' * comparing the parses.  Regenerate with tools/efx/gen_embedded.py',
    ' * rather than editing. */',
    '',
    'typedef struct {',
    '\tconst char *mapname;',
    '\tconst char *text;',
    '\tint length;',
    '} efxEmbedded_t;',
    '']
    names=[]
    for f in sorted(glob.glob(os.path.join(srcdir,'*.efx'))):
        name=os.path.basename(f)[:-4]
        data=open(f,'rb').read()
        names.append((name,len(data)))
        lit=[]
        for line in data.split(b'\n'):
            s=line.decode('latin-1')
            s=s.replace('\\','\\\\').replace('"','\\"').replace('\r','\\r')
            lit.append('\t"%s\\n"'%s)
        if data.endswith(b'\n'):
            lit=lit[:-1]
        out.append('static const char efx_%s[] ='%name)
        out.extend(lit)
        out[-1]=out[-1]+';'
        out.append('')
    out.append('static const efxEmbedded_t efx_embedded[] = {')
    for n,l in names:
        out.append('\t{ "%s", efx_%s, %d },'%(n,n,l))
    out.append('};')
    out.append('')
    open(outpath,'w').write('\n'.join(out))
    print("entries:", len(names))
    # The build has no header-dependency tracking for this file, so a
    # regenerated table with an untouched consumer links the OLD table -
    # exactly the stale-object failure that hit the first 51-entry run.
    consumer = os.path.join(os.path.dirname(outpath), 'snd_efxfile.cpp')
    if os.path.exists(consumer):
        os.utime(consumer, None)
        print("touched", consumer, "(no header dep tracking in the Makefile)")

if __name__=='__main__':
    main(sys.argv[1], sys.argv[2])
