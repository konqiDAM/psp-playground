/*
 * Host tests for the resumable download engine (fake transport/storage).
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "checksum.h"
#include "download.h"
#include "download_state.h"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define TARGET "ms0:/PSP/GAME/HOMEBREW/App/EBOOT.PBP"
#define PART TARGET ".part"
#define RESUME TARGET ".resume"
#define NEWP TARGET ".new"
#define SHA_ZERO "0000000000000000000000000000000000000000000000000000000000000000"

typedef struct FakeStop FakeStop;

typedef struct {
    char root[256];
    uint32_t free_bytes;
    int free_unknown;
    int fail_remove;
    int fail_rename;
    long full_after;
    long written;
    int sync_count;
    int write_count;
    int fail_read;
    int fail_stat_target;
    int fail_stat_new;
    int suspend_on_read_at;
    int suspend_after_read_at;
    int read_count;
    int suspend_rename_part_new_after;
    int suspend_size_target_after;
    int suspend_size_target_missing;
    int suspend_rename_new_target_after;
    int suspend_size_new_after;
    int suspend_remove_new_after;
    int suspend_after_mkdir_at;
    int mkdir_count;
    int flag_sync_count;
    int flag_write_count;
    FakeStop *stop;
} FakeStorage;

typedef struct {
    FILE *fp;
    FakeStorage *storage;
} FakeFile;

struct FakeStop {
    uint32_t done;
    uint32_t at;
    int err;
    int force_stop;
    int suspend_on_phase;
};

typedef struct {
    uint8_t body[300000];
    uint32_t body_len;
    int status;
    int has_cl;
    uint32_t cl;
    int has_cr;
    uint32_t rs, re, rt;
    int has_etag;
    int strong;
    char etag[80];
    int cut_at;
    uint32_t pos;
    uint32_t body_start;
    int chunk;
    int opened;
    int range_req;
    uint32_t range_start;
    char if_range[80];
    int fail_open;
    int net_err;
    FakeStop *stop;
    int suspend_in_open;
    int suspend_in_read_at;
    FakeStorage *storage;
    int sync_at_suspend;
    int write_at_suspend;
} FakeTransport;

static void fake_map(FakeStorage *s, const char *path, char *out, size_t size)
{
    const char *p = path;
    if (strncmp(p, "ms0:", 4) == 0)
        p += 4;
    snprintf(out, size, "%s%s", s->root, p);
}

static int fake_open(void *ctx, const char *path, int mode, void **file)
{
    FakeStorage *s = ctx;
    char real[512];
    FakeFile *f;
    const char *m;

    fake_map(s, path, real, sizeof(real));
    f = calloc(1, sizeof(*f));
    if (!f)
        return -1;
    f->storage = s;
    if (mode == DL_OPEN_READ)
        m = "rb";
    else if (mode == DL_OPEN_WRITE)
        m = "wb";
    else
        m = "r+b";
    f->fp = fopen(real, m);
    if (!f->fp && mode == DL_OPEN_APPEND)
        f->fp = fopen(real, "w+b");
    if (!f->fp) {
        free(f);
        return -1;
    }
    *file = f;
    return 0;
}

static void storage_suspend(FakeStorage *s)
{
    if (s->stop && !s->stop->force_stop) {
        s->stop->force_stop = DL_ERR_SUSPENDED;
        s->flag_sync_count = s->sync_count;
        s->flag_write_count = s->write_count;
    }
}

static int fake_read(void *file, void *buf, int len)
{
    FakeFile *f = file;
    size_t n;
    if (f->storage->fail_read)
        return -1;
    f->storage->read_count++;
    if (f->storage->suspend_on_read_at > 0 &&
        f->storage->read_count >= f->storage->suspend_on_read_at) {
        storage_suspend(f->storage);
        return -1;
    }
    n = fread(buf, 1, (size_t)len, f->fp);
    if (n == 0 && ferror(f->fp))
        return -1;
    if (f->storage->suspend_after_read_at > 0 &&
        f->storage->read_count >= f->storage->suspend_after_read_at)
        storage_suspend(f->storage);
    return (int)n;
}

static int fake_write(void *file, const void *buf, int len)
{
    FakeFile *f = file;
    if (fwrite(buf, 1, (size_t)len, f->fp) != (size_t)len)
        return -1;
    f->storage->write_count++;
    return len;
}

static int fake_seek(void *file, uint32_t offset)
{
    FakeFile *f = file;
    return fseek(f->fp, (long)offset, SEEK_SET) == 0 ? 0 : -1;
}

static int fake_sync(void *file)
{
    FakeFile *f = file;
    if (fflush(f->fp) != 0)
        return -1;
    if (fsync(fileno(f->fp)) != 0)
        return -1;
    f->storage->sync_count++;
    return 0;
}

static int fake_close(void *file)
{
    FakeFile *f = file;
    int r = fclose(f->fp);
    free(f);
    return r == 0 ? 0 : -1;
}

static int fake_size(void *ctx, const char *path, uint32_t *out)
{
    FakeStorage *s = ctx;
    char real[512];
    struct stat st;

    if (s->fail_stat_target && strcmp(path, TARGET) == 0)
        return -1;
    if (s->fail_stat_new && strcmp(path, NEWP) == 0)
        return -1;
    fake_map(s, path, real, sizeof(real));
    if (stat(real, &st) != 0) {
        if (errno == ENOENT) {
            if (strcmp(path, TARGET) == 0 && s->suspend_size_target_missing)
                storage_suspend(s);
            return 1;
        }
        return -1;
    }
    *out = (uint32_t)st.st_size;
    if (strcmp(path, TARGET) == 0 && s->suspend_size_target_after)
        storage_suspend(s);
    if (strcmp(path, NEWP) == 0 && s->suspend_size_new_after)
        storage_suspend(s);
    return 0;
}

static int fake_remove(void *ctx, const char *path)
{
    FakeStorage *s = ctx;
    char real[512];

    if (s->fail_remove)
        return -1;
    fake_map(s, path, real, sizeof(real));
    if (remove(real) != 0)
        return errno == ENOENT ? 1 : -1;
    if (strcmp(path, NEWP) == 0 && s->suspend_remove_new_after)
        storage_suspend(s);
    return 0;
}

static int fake_rename(void *ctx, const char *from, const char *to)
{
    FakeStorage *s = ctx;
    char a[512], b[512];
    struct stat st;

    if (s->fail_rename)
        return -1;
    fake_map(s, from, a, sizeof(a));
    fake_map(s, to, b, sizeof(b));
    if (stat(b, &st) == 0)
        return -1; /* FAT: no replace */
    if (rename(a, b) != 0)
        return -1;
    if (strcmp(from, PART) == 0 && strcmp(to, NEWP) == 0 &&
        s->suspend_rename_part_new_after)
        storage_suspend(s);
    if (strcmp(from, NEWP) == 0 && strcmp(to, TARGET) == 0 &&
        s->suspend_rename_new_target_after)
        storage_suspend(s);
    return 0;
}

static int fake_mkdir(void *ctx, const char *path)
{
    FakeStorage *s = ctx;
    char real[512];

    fake_map(s, path, real, sizeof(real));
    {
        int r = mkdir(real, 0777);
        if (r != 0 && errno != EEXIST)
            return -1;
        s->mkdir_count++;
        if (s->suspend_after_mkdir_at == s->mkdir_count)
            storage_suspend(s);
        return r == 0 ? 0 : 1;
    }
}

static int fake_free(void *ctx, uint32_t *out)
{
    FakeStorage *s = ctx;
    if (s->free_unknown)
        return 1;
    *out = s->free_bytes;
    return 0;
}

static int fake_should_stop(void *ctx)
{
    FakeStop *s = ctx;
    if (s->force_stop)
        return s->force_stop;
    if (s->at != 0 && s->done >= s->at)
        return s->err;
    return 0;
}

static void fake_progress(void *ctx, uint32_t done, uint32_t total, int phase)
{
    FakeStop *s = ctx;
    (void)total;
    s->done = done;
    if (s->suspend_on_phase && phase == s->suspend_on_phase)
        s->force_stop = DL_ERR_SUSPENDED;
}

static FakeStorage *storage_new(void)
{
    FakeStorage *s = calloc(1, sizeof(*s));
    char path[512];
    if (!s)
        return NULL;
    snprintf(s->root, sizeof(s->root), "/tmp/wifi-dl-XXXXXX");
    if (!mkdtemp(s->root)) {
        free(s);
        return NULL;
    }
    snprintf(path, sizeof(path), "%s/PSP", s->root);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), "%s/PSP/GAME", s->root);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), "%s/PSP/GAME/HOMEBREW", s->root);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), "%s/PSP/GAME/HOMEBREW/App", s->root);
    mkdir(path, 0777);
    s->free_bytes = 100u * 1024u * 1024u;
    s->full_after = -1;
    return s;
}

static void storage_free(FakeStorage *s)
{
    char cmd[512];
    if (!s)
        return;
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", s->root);
    if (system(cmd) != 0) { /* best effort */ }
    free(s);
}

static void store_write(FakeStorage *s, const char *path, const void *data,
                        uint32_t len)
{
    void *f;
    if (fake_open(s, path, DL_OPEN_WRITE, &f) == 0) {
        fake_write(f, data, (int)len);
        fake_close(f);
    }
}

static uint32_t store_size(FakeStorage *s, const char *path)
{
    uint32_t n = 0;
    return fake_size(s, path, &n) == 0 ? n : 0;
}

static void make_resume(FakeStorage *s, uint32_t size, const char *sha,
                        uint32_t offset, const char *etag)
{
    DlResume r;
    char text[512];
    int n;

    memset(&r, 0, sizeof(r));
    r.size = size;
    snprintf(r.sha256, sizeof(r.sha256), "%s", sha);
    snprintf(r.etag, sizeof(r.etag), "%s", etag ? etag : "");
    r.offset = offset;
    n = dl_resume_render(&r, text, sizeof(text));
    CHECK(n > 0);
    store_write(s, RESUME, text, (uint32_t)n);
}

static void body_fill(FakeTransport *t, uint32_t len, uint8_t seed)
{
    uint32_t i;
    t->body_len = len;
    for (i = 0; i < len; i++)
        t->body[i] = (uint8_t)(seed + i);
}

static int body_sha(FakeTransport *t, char out[65])
{
    Sha256 ctx;
    unsigned char digest[32];
    sha256_init(&ctx);
    sha256_update(&ctx, t->body, t->body_len);
    sha256_final(&ctx, digest);
    sha256_hex(digest, out);
    return 0;
}

static int t_open(void *ctx, const char *url, int has_range,
                  uint32_t range_start, const char *if_range,
                  DlResponse *out, int *net_err)
{
    FakeTransport *t = ctx;
    (void)url;
    t->opened++;
    t->range_req = has_range;
    t->range_start = range_start;
    snprintf(t->if_range, sizeof(t->if_range), "%s", if_range ? if_range : "");
    if (t->fail_open) {
        if (net_err)
            *net_err = t->net_err ? t->net_err : DL_ERR_NET;
        return -1;
    }
    if (t->suspend_in_open && t->stop)
        t->stop->force_stop = DL_ERR_SUSPENDED;
    memset(out, 0, sizeof(*out));
    out->status = t->status;
    out->has_content_length = t->has_cl;
    out->content_length = t->cl;
    out->has_content_range = t->has_cr;
    out->range_start = t->rs;
    out->range_end = t->re;
    out->range_total = t->rt;
    out->has_etag = t->has_etag;
    out->etag_strong = t->strong;
    snprintf(out->etag, sizeof(out->etag), "%s", t->etag);
    return 0;
}

static int t_read(void *ctx, void *buf, int max, int *net_err)
{
    FakeTransport *t = ctx;
    uint32_t left = t->body_len - t->pos;
    uint32_t n;
    (void)net_err;

    if (t->suspend_in_read_at >= 0 &&
        t->pos >= (uint32_t)t->suspend_in_read_at) {
        if (t->stop)
            t->stop->force_stop = DL_ERR_SUSPENDED;
        if (t->storage) {
            t->sync_at_suspend = t->storage->sync_count;
            t->write_at_suspend = t->storage->write_count;
        }
        if (net_err)
            *net_err = DL_ERR_SUSPENDED;
        return -1;
    }
    if (t->cut_at >= 0 && t->pos >= (uint32_t)t->cut_at)
        return 0;
    if (left == 0)
        return 0;
    n = (uint32_t)max < left ? (uint32_t)max : left;
    if (t->chunk > 0 && n > (uint32_t)t->chunk)
        n = (uint32_t)t->chunk;
    if (t->cut_at >= 0 && t->pos + n > (uint32_t)t->cut_at)
        n = (uint32_t)t->cut_at - t->pos;
    memcpy(buf, t->body + t->body_start + t->pos, n);
    t->pos += n;
    return (int)n;
}

static void t_close(void *ctx)
{
    (void)ctx;
}

static void env_init(DlEnv *env, FakeStorage *s, FakeTransport *t,
                     FakeStop *stop, uint8_t *buf, int buf_size)
{
    memset(env, 0, sizeof(*env));
    env->transport.open = t_open;
    env->transport.read = t_read;
    env->transport.close = t_close;
    env->transport.ctx = t;
    env->storage.ctx = s;
    env->storage.open = fake_open;
    env->storage.read = fake_read;
    env->storage.write = fake_write;
    env->storage.seek = fake_seek;
    env->storage.sync = fake_sync;
    env->storage.close = fake_close;
    env->storage.size = fake_size;
    env->storage.remove = fake_remove;
    env->storage.rename = fake_rename;
    env->storage.mkdir = fake_mkdir;
    env->storage.free_bytes = fake_free;
    env->buffer = buf;
    env->buffer_size = (size_t)buf_size;
    env->should_stop = fake_should_stop;
    env->stop_ctx = stop;
    env->progress = fake_progress;
    env->progress_ctx = stop;
    t->stop = stop;
    s->stop = stop;
    t->storage = s;
}

static DlSpec spec_for(FakeTransport *t, const char *sha)
{
    DlSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.url = "http://host/apps/a.pbp";
    spec.target_path = TARGET;
    spec.expected_size = t->body_len;
    spec.expected_sha256 = sha;
    spec.entry_id = "app";
    return spec;
}

static void transport_200(FakeTransport *t, uint32_t len)
{
    memset(t, 0, sizeof(*t));
    body_fill(t, len, 0x40);
    t->status = 200;
    t->has_cl = 1;
    t->cl = len;
    t->has_etag = 1;
    t->strong = 1;
    snprintf(t->etag, sizeof(t->etag), "\"v1\"");
    t->cut_at = -1;
    t->suspend_in_read_at = -1;
}

static void test_fresh_200(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_OK);
    CHECK(done == 1000);
    CHECK(store_size(s, TARGET) == 1000);
    CHECK(fake_size(s, PART, &done) == 1);
    CHECK(fake_size(s, RESUME, &done) == 1);
    CHECK(t.opened == 1 && !t.range_req);
    storage_free(s);
}

static void test_resume_206(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    store_write(s, PART, t.body, 400);
    make_resume(s, 1000, sha, 400, "\"v1\"");
    t.body_len = 600;
    t.body_start = 400;
    t.pos = 0;
    t.status = 206;
    t.has_cr = 1;
    t.rs = 400;
    t.re = 999;
    t.rt = 1000;
    t.cl = 600;
    spec = spec_for(&t, sha);
    spec.expected_size = 1000;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_OK);
    CHECK(done == 1000);
    CHECK(store_size(s, TARGET) == 1000);
    CHECK(t.range_req == 1 && t.range_start == 400);
    CHECK(strcmp(t.if_range, "\"v1\"") == 0);
    storage_free(s);
}

static void test_200_restarts(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    store_write(s, PART, t.body, 400);
    make_resume(s, 1000, sha, 400, "\"v1\"");
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_OK);
    CHECK(done == 1000);
    CHECK(store_size(s, TARGET) == 1000);
    storage_free(s);
}

static void test_206_wrong_start(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    store_write(s, PART, t.body, 400);
    make_resume(s, 1000, sha, 400, "\"v1\"");
    t.body_len = 700;
    t.status = 206;
    t.has_cr = 1;
    t.rs = 300;
    t.re = 999;
    t.rt = 1000;
    t.cl = 700;
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_BAD_RESPONSE);
    CHECK(fake_size(s, PART, &done) == 1);
    CHECK(fake_size(s, RESUME, &done) == 1);
    storage_free(s);
}

static void test_206_wrong_total(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    store_write(s, PART, t.body, 400);
    make_resume(s, 1000, sha, 400, "\"v1\"");
    t.body_len = 700;
    t.status = 206;
    t.has_cr = 1;
    t.rs = 400;
    t.re = 998;
    t.rt = 999;
    t.cl = 599;
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_BAD_RESPONSE);
    storage_free(s);
}

static void test_416(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    store_write(s, PART, t.body, 400);
    make_resume(s, 1000, sha, 400, "\"v1\"");
    t.status = 416;
    t.has_cl = 0;
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_RANGE_REJECTED);
    CHECK(fake_size(s, PART, &done) == 1);
    CHECK(fake_size(s, RESUME, &done) == 1);
    storage_free(s);
}

static void test_changed_etag_restarts(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    store_write(s, PART, t.body, 400);
    make_resume(s, 1000, sha, 400, "\"v1\"");
    snprintf(t.etag, sizeof(t.etag), "\"v2\"");
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_OK);
    CHECK(done == 1000);
    CHECK(store_size(s, TARGET) == 1000);
    storage_free(s);
}

static void test_short_body(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];
    DlResume r;
    char text[512];
    void *f;
    int n;

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    t.cut_at = 600;
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_NET);
    CHECK(store_size(s, PART) == 600);
    CHECK(fake_open(s, RESUME, DL_OPEN_READ, &f) == 0);
    n = fake_read(f, text, sizeof(text) - 1);
    fake_close(f);
    CHECK(n > 0);
    text[n] = '\0';
    CHECK(dl_resume_parse(text, n, &r) == 0);
    CHECK(r.offset == 600);
    storage_free(s);
}

static void test_overlong_body(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    t.body_len = 1001;
    spec = spec_for(&t, sha);
    spec.expected_size = 1000;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_BAD_RESPONSE);
    storage_free(s);
}

static void test_checksum_mismatch(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    spec = spec_for(&t, SHA_ZERO);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_CHECKSUM);
    CHECK(fake_size(s, PART, &done) == 1);
    CHECK(fake_size(s, RESUME, &done) == 1);
    storage_free(s);
}

static void test_no_space(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    s->free_bytes = 1000 + 8u * 1024u * 1024u - 1u;
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_NO_SPACE);
    CHECK(t.opened == 0);
    storage_free(s);
}

static void test_cancel(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    t.chunk = 100;
    stop.at = 300;
    stop.err = DL_ERR_CANCELLED;
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_CANCELLED);
    CHECK(store_size(s, PART) == 300);
    CHECK(store_size(s, RESUME) > 0);
    storage_free(s);
}

static void test_suspend(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    t.chunk = 100;
    stop.at = 300;
    stop.err = DL_ERR_SUSPENDED;
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, PART) == 0);
    storage_free(s);
}

static void test_offset_equals_size(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    store_write(s, PART, t.body, 1000);
    make_resume(s, 1000, sha, 1000, "\"v1\"");
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_OK);
    CHECK(t.opened == 0);
    CHECK(store_size(s, TARGET) == 1000);
    storage_free(s);
}

static void test_self_id_refused(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    spec.entry_id = "wifi";
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SELF);
    CHECK(t.opened == 0);
    storage_free(s);
}

static void test_target_paths(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    spec.target_path = "ms0:/PSP/GAME/HOMEBREW/App/APP.PBP";
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_PATH);

    spec = spec_for(&t, sha);
    spec.target_path = "ms0:/PSP/GAME/OTHER/EBOOT.PBP";
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_PATH);
    storage_free(s);
}

static void test_recover_new_only(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    DlEnv env;
    FakeStop stop;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, NEWP, t.body, 1000);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_recover(&env, &spec) == DL_RECOVER_INSTALLED);
    CHECK(store_size(s, TARGET) == 1000);
    CHECK(fake_size(s, NEWP, &done) == 1);
    storage_free(s);
}

static void test_recover_new_and_target(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    DlEnv env;
    FakeStop stop;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];
    uint8_t old[10];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    memset(old, 0x55, sizeof(old));
    store_write(s, TARGET, old, sizeof(old));
    store_write(s, NEWP, t.body, 1000);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_recover(&env, &spec) == DL_RECOVER_NONE);
    CHECK(store_size(s, TARGET) == sizeof(old));
    CHECK(fake_size(s, NEWP, &done) == 1);
    storage_free(s);
}

static void test_recover_part_complete(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    DlEnv env;
    FakeStop stop;
    DlSpec spec;
    uint8_t buf[4096];
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, PART, t.body, 1000);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_recover(&env, &spec) == DL_RECOVER_READY);
    storage_free(s);
}

static void test_recover_part_incomplete(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    DlEnv env;
    FakeStop stop;
    DlSpec spec;
    uint8_t buf[4096];
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, PART, t.body, 500);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_recover(&env, &spec) == DL_RECOVER_NONE);
    storage_free(s);
}

static void test_install_remove_fails(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];
    uint8_t old[10];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    memset(old, 0x55, sizeof(old));
    store_write(s, TARGET, old, sizeof(old));
    s->fail_remove = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_STORAGE);
    CHECK(store_size(s, TARGET) == sizeof(old));
    CHECK(store_size(s, NEWP) == 1000);
    storage_free(s);
}

static void test_install_rename_fails(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    s->fail_rename = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_STORAGE);
    CHECK(store_size(s, PART) == 1000);
    storage_free(s);
}

static void test_target_length(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];
    char long_path[220];
    int i;

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    strcpy(long_path, "ms0:/PSP/GAME/HOMEBREW/");
    for (i = 0; i < 150; i++)
        strcat(long_path, "A");
    strcat(long_path, "/EBOOT.PBP");
    spec.target_path = long_path;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_PATH);
    storage_free(s);
}

static void test_target_length_160(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];
    char path[220];
    char *p = path;

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    p += sprintf(p, "ms0:/PSP/GAME/HOMEBREW/");
    memset(p, 'A', 127);
    p += 127;
    strcpy(p, "/EBOOT.PBP");
    spec = spec_for(&t, sha);
    spec.target_path = path;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(strlen(path) == 160);
    CHECK(dl_download_once(&env, &spec, &done) == DL_OK);
    CHECK(done == 1000);
    storage_free(s);
}

static void test_resumed_zero_bytes(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    uint32_t received = 99;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    store_write(s, PART, t.body, 400);
    make_resume(s, 1000, sha, 400, "\"v1\"");
    t.body_len = 0;
    t.body_start = 400;
    t.status = 206;
    t.has_cr = 1;
    t.rs = 400;
    t.re = 999;
    t.rt = 1000;
    t.cl = 600;
    spec = spec_for(&t, sha);
    spec.expected_size = 1000;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    env.attempt_bytes = &received;
    received = 0;
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_NET);
    CHECK(done == 400);
    CHECK(received == 0);
    storage_free(s);
}

static void test_suspend_in_read(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    t.chunk = 100;
    t.suspend_in_read_at = 400;
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, PART) == 0);
    CHECK(store_size(s, RESUME) > 0);
    CHECK(s->sync_count == t.sync_at_suspend);
    CHECK(s->write_count == t.write_at_suspend);
    storage_free(s);
}

static void test_suspend_in_open(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    t.suspend_in_open = 1;
    spec = spec_for(&t, sha);
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, PART) == 0);
    CHECK(s->sync_count == 0);
    CHECK(s->write_count == 0);
    storage_free(s);
}

static void check_no_post_flag_io(FakeStorage *s)
{
    CHECK(s->sync_count == s->flag_sync_count);
    CHECK(s->write_count == s->flag_write_count);
}

static void test_recover_new_suspend_after_verify(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, NEWP, t.body, 1000);
    s->suspend_after_read_at = 2;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_recover(&env, &spec) == DL_RECOVER_NONE);
    CHECK(store_size(s, NEWP) == 1000);
    CHECK(store_size(s, TARGET) == 0);
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_engine_recover_new_suspend_stops(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, NEWP, t.body, 1000);
    s->suspend_after_read_at = 2;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(t.opened == 0);
    CHECK(store_size(s, NEWP) == 1000);
    CHECK(store_size(s, TARGET) == 0);
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_recover_new_target_present_suspend_keeps_new(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    char sha[65];
    uint8_t old[10];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    memset(old, 0x55, sizeof(old));
    store_write(s, TARGET, old, sizeof(old));
    store_write(s, NEWP, t.body, 1000);
    s->suspend_size_new_after = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_recover(&env, &spec) == DL_RECOVER_NONE);
    CHECK(store_size(s, NEWP) == 1000);
    CHECK(store_size(s, TARGET) == sizeof(old));
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_recover_new_post_rename_keeps_resume(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, NEWP, t.body, 1000);
    make_resume(s, 1000, sha, 1000, "\"v1\"");
    s->suspend_rename_new_target_after = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_recover(&env, &spec) == DL_RECOVER_INSTALLED);
    CHECK(store_size(s, TARGET) == 1000);
    CHECK(fake_size(s, NEWP, &done) == 1);
    CHECK(store_size(s, RESUME) > 0);
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_engine_recover_new_post_rename_suspend(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, NEWP, t.body, 1000);
    make_resume(s, 1000, sha, 1000, "\"v1\"");
    s->suspend_rename_new_target_after = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(t.opened == 0);
    CHECK(store_size(s, TARGET) == 1000);
    CHECK(store_size(s, NEWP) == 0);
    CHECK(store_size(s, RESUME) > 0);
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_recover_new_suspend_after_ensure_dirs(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, NEWP, t.body, 1000);
    s->suspend_after_mkdir_at = 2;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_recover(&env, &spec) == DL_RECOVER_NONE);
    CHECK(store_size(s, NEWP) == 1000);
    CHECK(store_size(s, TARGET) == 0);
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_install_suspend_after_stale_new_remove(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];
    uint8_t stale[10];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    memset(stale, 0x66, sizeof(stale));
    store_write(s, NEWP, stale, sizeof(stale));
    store_write(s, PART, t.body, 1000);
    make_resume(s, 1000, sha, 1000, "\"v1\"");
    s->fail_stat_new = 1; /* keep the stale .new through recovery */
    s->suspend_remove_new_after = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, PART) == 1000);
    CHECK(store_size(s, NEWP) == 0);
    CHECK(store_size(s, TARGET) == 0);
    CHECK(store_size(s, RESUME) > 0);
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_install_suspend_after_ensure_dirs(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, PART, t.body, 1000);
    make_resume(s, 1000, sha, 1000, "\"v1\"");
    s->suspend_after_mkdir_at = 4; /* entry ensure_dirs + install ensure_dirs */
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, PART) == 1000);
    CHECK(store_size(s, NEWP) == 0);
    CHECK(store_size(s, TARGET) == 0);
    CHECK(store_size(s, RESUME) > 0);
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_install_suspend_after_part_new(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, PART, t.body, 1000);
    make_resume(s, 1000, sha, 1000, "\"v1\"");
    s->suspend_rename_part_new_after = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, PART) == 0);
    CHECK(store_size(s, NEWP) == 1000);
    CHECK(store_size(s, TARGET) == 0);
    CHECK(store_size(s, RESUME) > 0);
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_install_suspend_before_target_remove(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, PART, t.body, 1000);
    make_resume(s, 1000, sha, 1000, "\"v1\"");
    s->suspend_size_target_missing = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, PART) == 0);
    CHECK(store_size(s, NEWP) == 1000);
    CHECK(store_size(s, TARGET) == 0);
    CHECK(store_size(s, RESUME) > 0);
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_install_suspend_before_final_rename(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];
    uint8_t old[10];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    memset(old, 0x55, sizeof(old));
    store_write(s, TARGET, old, sizeof(old));
    store_write(s, PART, t.body, 1000);
    make_resume(s, 1000, sha, 1000, "\"v1\"");
    s->suspend_size_target_after = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, NEWP) == 1000);
    CHECK(store_size(s, TARGET) == sizeof(old));
    CHECK(store_size(s, RESUME) > 0);
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_install_suspend_after_final_rename(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, PART, t.body, 1000);
    make_resume(s, 1000, sha, 1000, "\"v1\"");
    s->suspend_rename_new_target_after = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, NEWP) == 0);
    CHECK(store_size(s, TARGET) == 1000);
    CHECK(store_size(s, RESUME) > 0);
    check_no_post_flag_io(s);
    storage_free(s);
}

static void test_suspend_in_verify(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    s->suspend_on_read_at = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, PART) == 1000);
    CHECK(store_size(s, RESUME) > 0);
    storage_free(s);
}

static void test_suspend_before_install(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    stop.suspend_on_phase = DL_PHASE_INSTALLING;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, TARGET) == 0);
    CHECK(store_size(s, PART) == 1000);
    CHECK(store_size(s, RESUME) > 0);
    storage_free(s);
}

static void test_suspend_in_recovered_verify(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, PART, t.body, 1000);
    make_resume(s, 1000, sha, 1000, "\"v1\"");
    s->suspend_on_read_at = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_SUSPENDED);
    CHECK(store_size(s, TARGET) == 0);
    CHECK(store_size(s, PART) == 1000);
    storage_free(s);
}

static void test_verify_storage_error_keeps_part(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    s->fail_read = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_download_once(&env, &spec, &done) == DL_ERR_STORAGE);
    CHECK(store_size(s, PART) == 1000);
    CHECK(store_size(s, RESUME) > 0);
    storage_free(s);
}

static void test_recover_target_stat_error_keeps_new(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    DlEnv env;
    FakeStop stop;
    DlSpec spec;
    uint8_t buf[4096];
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, NEWP, t.body, 1000);
    s->fail_stat_target = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_recover(&env, &spec) == DL_RECOVER_NONE);
    CHECK(store_size(s, NEWP) == 1000);
    storage_free(s);
}

static void test_recover_new_verify_storage_error_keeps_new(void)
{
    FakeStorage *s = storage_new();
    FakeTransport t;
    DlEnv env;
    FakeStop stop;
    DlSpec spec;
    uint8_t buf[4096];
    char sha[65];

    memset(&stop, 0, sizeof(stop));
    transport_200(&t, 1000);
    body_sha(&t, sha);
    spec = spec_for(&t, sha);
    store_write(s, NEWP, t.body, 1000);
    s->fail_read = 1;
    env_init(&env, s, &t, &stop, buf, sizeof(buf));
    CHECK(dl_recover(&env, &spec) == DL_RECOVER_NONE);
    CHECK(store_size(s, NEWP) == 1000);
    storage_free(s);
}

static void test_chunks(void){
    FakeStorage *s;
    FakeTransport t;
    FakeStop stop;
    DlEnv env;
    DlSpec spec;
    uint8_t buf[4096];
    uint32_t done = 0;
    char sha[65];
    int chunks[2] = { 1, 65536 };
    int i;

    for (i = 0; i < 2; i++) {
        s = storage_new();
        memset(&stop, 0, sizeof(stop));
        transport_200(&t, 1000);
        body_sha(&t, sha);
        t.chunk = chunks[i];
        spec = spec_for(&t, sha);
        env_init(&env, s, &t, &stop, buf, sizeof(buf));
        CHECK(dl_download_once(&env, &spec, &done) == DL_OK);
        CHECK(done == 1000);
        storage_free(s);
    }
}

int main(void)
{
    test_fresh_200();
    test_resume_206();
    test_200_restarts();
    test_206_wrong_start();
    test_206_wrong_total();
    test_416();
    test_changed_etag_restarts();
    test_short_body();
    test_overlong_body();
    test_checksum_mismatch();
    test_no_space();
    test_cancel();
    test_suspend();
    test_offset_equals_size();
    test_self_id_refused();
    test_target_paths();
    test_recover_new_only();
    test_recover_new_and_target();
    test_recover_part_complete();
    test_recover_part_incomplete();
    test_target_length_160();
    test_resumed_zero_bytes();
    test_suspend_in_read();
    test_suspend_in_open();
    test_suspend_in_verify();
    test_recover_new_suspend_after_verify();
    test_engine_recover_new_suspend_stops();
    test_recover_new_target_present_suspend_keeps_new();
    test_recover_new_post_rename_keeps_resume();
    test_engine_recover_new_post_rename_suspend();
    test_recover_new_suspend_after_ensure_dirs();
    test_install_suspend_after_stale_new_remove();
    test_install_suspend_after_ensure_dirs();
    test_install_suspend_after_part_new();
    test_install_suspend_before_target_remove();
    test_install_suspend_before_final_rename();
    test_install_suspend_after_final_rename();
    test_suspend_before_install();
    test_suspend_in_recovered_verify();
    test_verify_storage_error_keeps_part();
    test_recover_target_stat_error_keeps_new();
    test_recover_new_verify_storage_error_keeps_new();
    test_install_remove_fails();
    test_install_rename_fails();
    test_target_length();
    test_chunks();

    if (failures) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("all download engine tests passed\n");
    return 0;
}
