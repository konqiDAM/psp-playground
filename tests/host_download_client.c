/*
 * host_download_client.c - host integration scenarios against the mock server.
 */
#define _POSIX_C_SOURCE 200809L

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
#include "host_download_io.h"

#define FIXTURE_SIZE 262144u
#define TARGET "ms0:/PSP/GAME/HOMEBREW/App/EBOOT.PBP"
#define PART TARGET ".part"
#define RESUME TARGET ".resume"
#define NEWP TARGET ".new"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void fixture(uint8_t *out, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        out[i] = (uint8_t)(i & 0xff);
}

static void fixture_sha(char out[65])
{
    static uint8_t buf[FIXTURE_SIZE];
    Sha256 ctx;
    unsigned char digest[32];
    fixture(buf, sizeof(buf));
    sha256_init(&ctx);
    sha256_update(&ctx, buf, sizeof(buf));
    sha256_final(&ctx, digest);
    sha256_hex(digest, out);
}

static void ensure_root(const char *root)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/PSP", root);
    mkdir(p, 0777);
    snprintf(p, sizeof(p), "%s/PSP/GAME", root);
    mkdir(p, 0777);
    snprintf(p, sizeof(p), "%s/PSP/GAME/HOMEBREW", root);
    mkdir(p, 0777);
    snprintf(p, sizeof(p), "%s/PSP/GAME/HOMEBREW/App", root);
    mkdir(p, 0777);
}

static void write_file(HostStorage *s, const char *path, const void *data,
                       uint32_t len)
{
    DlStorage st;
    void *f = NULL;
    host_storage_env(&st, s);
    if (st.open(st.ctx, path, DL_OPEN_WRITE, &f) != 0)
        return;
    (void)st.write(f, data, (int)len);
    (void)st.close(f);
}

static uint32_t file_size(HostStorage *s, const char *path)
{
    DlStorage st;
    uint32_t n = 0;
    host_storage_env(&st, s);
    return st.size(st.ctx, path, &n) == 0 ? n : 0;
}

static void seed_resume(HostStorage *s, const char *sha)
{
    static uint8_t buf[100000];
    DlResume r;
    char text[512];
    int n;

    fixture(buf, sizeof(buf));
    write_file(s, PART, buf, sizeof(buf));
    memset(&r, 0, sizeof(r));
    r.size = FIXTURE_SIZE;
    snprintf(r.sha256, sizeof(r.sha256), "%s", sha);
    snprintf(r.etag, sizeof(r.etag), "\"v1\"");
    r.offset = (uint32_t)sizeof(buf);
    n = dl_resume_render(&r, text, sizeof(text));
    write_file(s, RESUME, text, (uint32_t)n);
}

static void clean_files(HostStorage *s)
{
    DlStorage st;
    host_storage_env(&st, s);
    (void)st.remove(st.ctx, TARGET);
    (void)st.remove(st.ctx, PART);
    (void)st.remove(st.ctx, RESUME);
    (void)st.remove(st.ctx, NEWP);
}

int main(int argc, char **argv)
{
    const char *scenario;
    const char *base;
    const char *root;
    HostStorage storage;
    HostTransport transport;
    DlStorage st;
    DlTransport tr;
    DlEnv env;
    DlSpec spec;
    uint8_t *buf;
    char sha[65];
    char url[256];
    DlError err = DL_ERR_NET;
    int attempts = 0;
    int i;

    if (argc != 4) {
        fprintf(stderr, "usage: %s scenario base_url root\n", argv[0]);
        return 2;
    }
    scenario = argv[1];
    base = argv[2];
    root = argv[3];

    ensure_root(root);
    host_storage_init(&storage, root);
    host_storage_env(&st, &storage);
    host_transport_init(&transport);
    host_transport_env(&tr, &transport);
    fixture_sha(sha);

    clean_files(&storage);

    memset(&spec, 0, sizeof(spec));
    spec.target_path = TARGET;
    spec.expected_size = FIXTURE_SIZE;
    spec.expected_sha256 = sha;
    spec.entry_id = "app";

    snprintf(url, sizeof(url), "%s/apps/fixture.pbp", base);
    if (strcmp(scenario, "resume") == 0)
        snprintf(url, sizeof(url), "%s/apps/fixture-cut.pbp", base);
    else if (strcmp(scenario, "cut-twice") == 0)
        snprintf(url, sizeof(url), "%s/apps/fixture-cut2.pbp", base);
    else if (strcmp(scenario, "etag-changed") == 0)
        snprintf(url, sizeof(url), "%s/apps/fixture-etag2.pbp", base);
    else if (strcmp(scenario, "range-rejected") == 0)
        snprintf(url, sizeof(url), "%s/apps/fixture-416.pbp", base);
    else if (strcmp(scenario, "rejected-chunked") == 0)
        snprintf(url, sizeof(url), "%s/apps/fixture-chunked.pbp", base);
    else if (strcmp(scenario, "rejected-gzip") == 0)
        snprintf(url, sizeof(url), "%s/apps/fixture-gzip.pbp", base);
    else if (strcmp(scenario, "checksum-mismatch") == 0)
        spec.expected_sha256 =
            "0000000000000000000000000000000000000000000000000000000000000000";
    spec.url = url;

    if (strcmp(scenario, "etag-changed") == 0 ||
        strcmp(scenario, "range-rejected") == 0)
        seed_resume(&storage, sha);

    buf = malloc(1024u * 1024u);
    if (!buf)
        return 2;
    memset(&env, 0, sizeof(env));
    env.transport = tr;
    env.storage = st;
    env.buffer = buf;
    env.buffer_size = 1024u * 1024u;

    for (i = 0; i < 8; i++) {
        attempts++;
        err = dl_download_once(&env, &spec, NULL);
        if (err == DL_OK)
            break;
        if (err == DL_ERR_NET || err == DL_ERR_TIMEOUT ||
            err == DL_ERR_STALL || err == DL_ERR_CONNECTION_LOST ||
            err == DL_ERR_DNS)
            continue;
        break;
    }

    if (strcmp(scenario, "fresh") == 0) {
        CHECK(err == DL_OK);
        CHECK(attempts == 1);
        CHECK(file_size(&storage, TARGET) == FIXTURE_SIZE);
        if (err == DL_OK && attempts == 1)
            printf("PASS fresh bytes=%u\n", FIXTURE_SIZE);
    } else if (strcmp(scenario, "resume") == 0) {
        CHECK(err == DL_OK);
        CHECK(attempts == 2);
        CHECK(file_size(&storage, TARGET) == FIXTURE_SIZE);
        if (err == DL_OK && attempts == 2)
            printf("PASS resume first_cut=100000 bytes=%u\n", FIXTURE_SIZE);
    } else if (strcmp(scenario, "etag-changed") == 0) {
        CHECK(err == DL_OK);
        CHECK(transport.last_status == 200);
        CHECK(file_size(&storage, TARGET) == FIXTURE_SIZE);
        if (err == DL_OK && transport.last_status == 200)
            printf("PASS etag-changed restarted=1 bytes=%u\n", FIXTURE_SIZE);
    } else if (strcmp(scenario, "range-rejected") == 0) {
        CHECK(err == DL_ERR_RANGE_REJECTED);
        if (err == DL_ERR_RANGE_REJECTED)
            printf("PASS range-rejected\n");
    } else if (strcmp(scenario, "rejected-chunked") == 0) {
        CHECK(err == DL_ERR_BAD_RESPONSE);
        if (err == DL_ERR_BAD_RESPONSE)
            printf("PASS rejected-chunked\n");
    } else if (strcmp(scenario, "rejected-gzip") == 0) {
        CHECK(err == DL_ERR_BAD_RESPONSE);
        if (err == DL_ERR_BAD_RESPONSE)
            printf("PASS rejected-gzip\n");
    } else if (strcmp(scenario, "checksum-mismatch") == 0) {
        CHECK(err == DL_ERR_CHECKSUM);
        if (err == DL_ERR_CHECKSUM)
            printf("PASS checksum-mismatch\n");
    } else if (strcmp(scenario, "cut-twice") == 0) {
        CHECK(err == DL_OK);
        CHECK(attempts == 3);
        CHECK(file_size(&storage, TARGET) == FIXTURE_SIZE);
        if (err == DL_OK && attempts == 3)
            printf("PASS cut-twice bytes=%u\n", FIXTURE_SIZE);
    } else {
        fprintf(stderr, "unknown scenario %s\n", scenario);
        free(buf);
        return 2;
    }

    free(buf);
    return failures ? 1 : 0;
}
