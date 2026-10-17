#!/usr/bin/env python3
"""Exercise the shipping vertex ring methods with a scripted GL backend."""
import os
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[2] / 'neo/renderer/VertexCache.cpp').read_text()

def method(signature):
    start = source.index(signature)
    end = source.index('\n/*', start)
    return source[start:end]

stub = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
typedef unsigned GLenum;
typedef unsigned GLuint;
typedef unsigned char byte;
typedef void *gl_caps_sync_t;
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_SYNC_FLUSH_COMMANDS_BIT 1
#define GL_ALREADY_SIGNALED 0x911a
#define GL_TIMEOUT_EXPIRED 0x911b
#define GL_CONDITION_SATISFIED 0x911c
#define GL_WAIT_FAILED 0x911d
#define VERTEX_ALIGN 16
static GLenum status[32];
static unsigned polls, deletes, fences;
static bool allocation_fails;
static gl_caps_sync_t fence(GLenum, unsigned) {
    if (allocation_fails) return NULL;
    status[++fences] = GL_TIMEOUT_EXPIRED;
    return reinterpret_cast<void *>(static_cast<size_t>(fences));
}
static GLenum poll(gl_caps_sync_t sync, unsigned flags, unsigned long long timeout) {
    assert(flags == GL_SYNC_FLUSH_COMMANDS_BIT && timeout == 0);
    ++polls;
    return status[reinterpret_cast<size_t>(sync)];
}
static void retire(gl_caps_sync_t sync) {
    GLenum &s = status[reinterpret_cast<size_t>(sync)];
    assert(s == GL_ALREADY_SIGNALED || s == GL_CONDITION_SATISFIED);
    s = 0;
    ++deletes;
}
static struct {
    gl_caps_sync_t (*FenceSync)(GLenum, unsigned);
    GLenum (*ClientWaitSync)(gl_caps_sync_t, unsigned, unsigned long long);
    void (*DeleteSync)(gl_caps_sync_t);
} glCaps = {fence, poll, retire};
struct idVertexCache {
    enum { RING_SEGMENTS = 3 };
    gl_caps_sync_t ringFences[3];
    bool ringFenceFailed[3], ringWritable;
    int ringSegment, ringSegmentBytes, ringUsed;
    byte *ringMemory;
    GLuint ringBuffer;
    void RingEndFrame();
    void *RingReserve(int, int, GLuint *, int *);
};
'''
checks = r'''
int main() {
    idVertexCache ring;
    std::memset(&ring, 0, sizeof(ring));
    byte storage[192] = {};
    ring.ringMemory = storage;
    ring.ringBuffer = 7;
    ring.ringSegmentBytes = 64;
    for (int i = 0; i < 3; ++i) ring.ringFences[i] = fence(0, 0);
    for (int i = 0; i < 10000; ++i) ring.RingEndFrame();
    assert(polls == 30000 && fences == 3 && deletes == 0);
    assert(!ring.ringWritable && ring.ringUsed == 64);
    GLuint buffer = 0;
    int offset = -1;
    assert(!ring.RingReserve(16, 16, &buffer, &offset));
    assert(buffer == 0 && offset == -1);
    status[2] = GL_WAIT_FAILED;
    ring.RingEndFrame();
    assert(!ring.ringWritable && !deletes);
    status[3] = GL_CONDITION_SATISFIED;
    ring.RingEndFrame();
    assert(ring.ringWritable && ring.ringSegment == 2 && ring.ringUsed == 0);
    assert(deletes == 1 && !ring.ringFences[2]);
    assert(ring.RingReserve(16, 16, &buffer, &offset) == storage + 128);
    assert(buffer == 7 && offset == 128);
    ring.RingEndFrame();
    assert(!ring.ringWritable && fences == 4);
    status[1] = GL_ALREADY_SIGNALED;
    ring.RingEndFrame();
    assert(ring.ringWritable && ring.ringSegment == 0 && deletes == 2);
    allocation_fails = true;
    ring.RingEndFrame();
    assert(ring.ringFenceFailed[0] && !ring.ringWritable);
    ring.RingEndFrame();
    assert(!ring.ringWritable && ring.ringUsed == 64);
    std::puts("PASS: bounded zero-timeout polls; no pending/failed reuse; overflow fallback; recovery");
}
'''
with tempfile.TemporaryDirectory(prefix='boom3-vertex-ring-') as temporary:
    folder = Path(temporary)
    test = folder / 'test.cpp'
    test.write_text(stub + method('void idVertexCache::RingEndFrame()') +
                    method('void *idVertexCache::RingReserve(') + checks)
    binary = folder / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra',
                    '-Werror', str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
