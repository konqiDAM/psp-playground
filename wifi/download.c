/*
 * download.c - one resumable, verified, atomically installed transfer.
 */
#include "download.h"

#include <stdio.h>
#include <string.h>

#include "catalog.h"
#include "checksum.h"
#include "download_state.h"

#define DL_FREE_MARGIN_BYTES (8u * 1024u * 1024u)

static int ci_equal(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb + ('a' - 'A'));
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int path_suffix(const char *base, const char *suffix,
                       char *out, int size)
{
    int n;

    if (!base || !suffix || !out || size <= 0)
        return -1;
    n = snprintf(out, (size_t)size, "%s%s", base, suffix);
    if (n < 0 || n >= size || n >= DL_PATH_MAX)
        return -1;
    return 0;
}

static int target_valid(const char *path)
{
    size_t n;
    static const char root[] = "ms0:/PSP/GAME/HOMEBREW/";

    if (!path)
        return 0;
    n = strlen(path);
    if (n < sizeof(root) + 10 || n >= DL_TARGET_MAX)
        return 0;
    if (strncmp(path, root, sizeof(root) - 1) != 0)
        return 0;
    if (strcmp(path + n - 10, "/EBOOT.PBP") != 0)
        return 0;
    if (strstr(path, "..") || strchr(path, '\\') || strchr(path, '?') ||
        strchr(path, '#') || strchr(path, '%'))
        return 0;
    return 1;
}

static int sha_valid(const char *sha)
{
    size_t i;
    if (!sha || strlen(sha) != 64)
        return 0;
    for (i = 0; i < 64; i++) {
        char c = sha[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

static int st_size(const DlEnv *env, const char *path, uint32_t *out)
{
    int r = env->storage.size(env->storage.ctx, path, out);
    if (r == 0 || r == 1)
        return r;
    return -1;
}

static int st_remove_ignore(const DlEnv *env, const char *path)
{
    int r = env->storage.remove(env->storage.ctx, path);
    return (r == 0 || r == 1) ? 0 : -1;
}

static void discard(const DlEnv *env, const char *part, const char *resume)
{
    (void)st_remove_ignore(env, part);
    (void)st_remove_ignore(env, resume);
}

static int save_resume(const DlEnv *env, const DlResume *r, const char *path)
{
    char text[DL_RESUME_READ_MAX];
    void *f = NULL;
    int n;

    n = dl_resume_render(r, text, sizeof(text));
    if (n <= 0)
        return -1;
    if (env->storage.open(env->storage.ctx, path, DL_OPEN_WRITE, &f) != 0)
        return -1;
    if (env->storage.write(f, text, n) != n ||
        env->storage.sync(f) != 0) {
        env->storage.close(f);
        return -1;
    }
    if (env->storage.close(f) != 0)
        return -1;
    return 0;
}

static DlError stop_now(const DlEnv *env)
{
    if (!env || !env->should_stop)
        return DL_OK;
    return (DlError)env->should_stop(env->stop_ctx);
}

static DlError verify_file(const DlEnv *env, const char *path, const char *sha)
{
    Sha256 ctx;
    unsigned char digest[32];
    char hex[65];
    void *f = NULL;
    int n;
    int eof = 0;

    if (!env->buffer || env->buffer_size == 0)
        return DL_ERR_STORAGE;
    {
        DlError se = stop_now(env);
        if (se != DL_OK)
            return se;
    }
    if (env->storage.open(env->storage.ctx, path, DL_OPEN_READ, &f) != 0)
        return DL_ERR_STORAGE;
    sha256_init(&ctx);
    while (!eof) {
        int want = (int)(env->buffer_size < DL_VERIFY_CHUNK
                             ? env->buffer_size : DL_VERIFY_CHUNK);
        DlError se = stop_now(env);
        if (se != DL_OK) {
            env->storage.close(f);
            return se;
        }
        n = env->storage.read(f, env->buffer, want);
        if (n < 0) {
            env->storage.close(f);
            se = stop_now(env);
            return se != DL_OK ? se : DL_ERR_STORAGE;
        }
        if (n == 0) {
            eof = 1;
        } else {
            sha256_update(&ctx, env->buffer, (size_t)n);
        }
    }
    env->storage.close(f);
    sha256_final(&ctx, digest);
    sha256_hex(digest, hex);
    return ci_equal(hex, sha) ? DL_OK : DL_ERR_CHECKSUM;
}

static int ensure_dirs(const DlEnv *env, const char *target)
{
    char folder[DL_TARGET_MAX];
    size_t n;
    int r;

    n = strlen(target);
    if (n < 10 || n >= sizeof(folder))
        return -1;
    memcpy(folder, target, n - 10);
    folder[n - 10] = '\0';

    r = env->storage.mkdir(env->storage.ctx, "ms0:/PSP/GAME/HOMEBREW");
    if (r < 0)
        return -1;
    r = env->storage.mkdir(env->storage.ctx, folder);
    if (r < 0)
        return -1;
    return 0;
}

static DlError install_part(const DlEnv *env, const DlSpec *spec,
                            const char *part, const char *resume,
                            const char *new_path)
{
    uint32_t existing = 0;
    int r;

    {
        DlError se = stop_now(env);
        if (se != DL_OK)
            return se;
    }
    if (ensure_dirs(env, spec->target_path) != 0)
        return DL_ERR_STORAGE;

    (void)st_remove_ignore(env, new_path);
    {
        DlError se = stop_now(env);
        if (se != DL_OK)
            return se;
    }
    if (env->storage.rename(env->storage.ctx, part, new_path) != 0)
        return DL_ERR_STORAGE;

    {
        DlError se = stop_now(env);
        if (se != DL_OK)
            return se;
    }
    r = st_size(env, spec->target_path, &existing);
    if (r < 0)
        return DL_ERR_STORAGE;
    if (r == 0) {
        DlError se = stop_now(env);
        if (se != DL_OK)
            return se;
        if (env->storage.remove(env->storage.ctx, spec->target_path) < 0)
            return DL_ERR_STORAGE;
    }
    {
        DlError se = stop_now(env);
        if (se != DL_OK)
            return se;
    }
    if (env->storage.rename(env->storage.ctx, new_path,
                            spec->target_path) != 0)
        return DL_ERR_STORAGE;
    {
        DlError se = stop_now(env);
        if (se != DL_OK)
            return se; /* target is installed; keep .resume for recovery */
    }
    (void)st_remove_ignore(env, resume);
    return DL_OK;
}

DlRecoverResult dl_recover(const DlEnv *env, const DlSpec *spec)
{
    char part[DL_PATH_MAX];
    char resume_path[DL_PATH_MAX];
    char new_path[DL_PATH_MAX];
    uint32_t size = 0;
    uint32_t target_size = 0;
    int r;
    int t;

    if (!env || !spec || !spec->target_path || !spec->expected_sha256 ||
        !env->buffer || env->buffer_size == 0)
        return DL_RECOVER_NONE;
    if (!target_valid(spec->target_path) ||
        !sha_valid(spec->expected_sha256))
        return DL_RECOVER_NONE;
    if (path_suffix(spec->target_path, ".part", part, sizeof(part)) != 0 ||
        path_suffix(spec->target_path, ".resume", resume_path,
                    sizeof(resume_path)) != 0 ||
        path_suffix(spec->target_path, ".new", new_path, sizeof(new_path)) != 0)
        return DL_RECOVER_NONE;

    {
        DlError se = stop_now(env);
        if (se != DL_OK)
            return DL_RECOVER_NONE;
    }
    r = st_size(env, new_path, &size);
    if (r == 0) {
        t = st_size(env, spec->target_path, &target_size);
        if (t == 1) {
            DlError vr = verify_file(env, new_path, spec->expected_sha256);
            DlError se;
            if (vr == DL_ERR_SUSPENDED)
                return DL_RECOVER_NONE;
            se = stop_now(env);
            if (se != DL_OK)
                return DL_RECOVER_NONE;
            if (vr == DL_OK) {
                if (ensure_dirs(env, spec->target_path) != 0)
                    return DL_RECOVER_NONE;
                se = stop_now(env);
                if (se != DL_OK)
                    return DL_RECOVER_NONE;
                if (env->storage.rename(env->storage.ctx, new_path,
                                        spec->target_path) != 0)
                    return DL_RECOVER_NONE;
                se = stop_now(env);
                if (se == DL_OK)
                    (void)st_remove_ignore(env, resume_path);
                return DL_RECOVER_INSTALLED;
            }
            if (vr == DL_ERR_CHECKSUM)
                (void)st_remove_ignore(env, new_path);
            return DL_RECOVER_NONE;
        }
        if (t < 0)
            return DL_RECOVER_NONE; /* keep .new for retry */
        {
            DlError se = stop_now(env);
            if (se != DL_OK)
                return DL_RECOVER_NONE; /* keep .new */
        }
        (void)st_remove_ignore(env, new_path);
        return DL_RECOVER_NONE;
    }
    if (r < 0)
        return DL_RECOVER_NONE;

    r = st_size(env, part, &size);
    if (r == 0 && size == spec->expected_size)
        return DL_RECOVER_READY;
    return DL_RECOVER_NONE;
}

DlError dl_download_once(const DlEnv *env, const DlSpec *spec,
                         uint32_t *bytes_done)
{
    char part[DL_PATH_MAX];
    char resume_path[DL_PATH_MAX];
    char new_path[DL_PATH_MAX];
    DlResume state;
    DlResponse resp;
    void *part_file = NULL;
    int transport_open = 0;
    int have_state = 0;
    uint32_t offset = 0;
    uint32_t done = 0;
    uint32_t part_size = 0;
    uint32_t used = 0;
    uint32_t last_checkpoint = 0;
    uint32_t free_bytes = 0;
    int r;
    int net_err = 0;
    DlError err = DL_OK;

    if (bytes_done)
        *bytes_done = 0;
    if (!env || !spec || !spec->url || !spec->target_path ||
        !env->buffer || env->buffer_size == 0 ||
        !env->transport.open || !env->transport.read ||
        !env->transport.close || !env->storage.size)
        return DL_ERR_ARG;

    if (!target_valid(spec->target_path) || !sha_valid(spec->expected_sha256))
        return DL_ERR_PATH;
    if (spec->entry_id && strcmp(spec->entry_id, "wifi") == 0)
        return DL_ERR_SELF;
    if (spec->expected_size < 1 ||
        spec->expected_size > CATALOG_MAX_FILE_BYTES)
        return DL_ERR_ARG;

    if (path_suffix(spec->target_path, ".part", part, sizeof(part)) != 0 ||
        path_suffix(spec->target_path, ".resume", resume_path,
                    sizeof(resume_path)) != 0 ||
        path_suffix(spec->target_path, ".new", new_path, sizeof(new_path)) != 0)
        return DL_ERR_PATH;

    if (env->should_stop) {
        int stop = env->should_stop(env->stop_ctx);
        if (stop != 0)
            return (DlError)stop;
    }

    if (ensure_dirs(env, spec->target_path) != 0)
        return DL_ERR_STORAGE;

    {
        DlRecoverResult rec = dl_recover(env, spec);
        if (rec == DL_RECOVER_INSTALLED) {
            DlError se = stop_now(env);
            if (bytes_done)
                *bytes_done = spec->expected_size;
            if (se != DL_OK)
                return se; /* target installed; .resume kept for next run */
            if (env->progress)
                env->progress(env->progress_ctx, spec->expected_size,
                              spec->expected_size, DL_PHASE_DONE);
            return DL_OK;
        }
        if (rec == DL_RECOVER_NONE) {
            DlError se = stop_now(env);
            if (se != DL_OK)
                return se;
        }
        if (rec == DL_RECOVER_READY) {
            DlError vr = verify_file(env, part, spec->expected_sha256);
            if (vr == DL_ERR_CHECKSUM) {
                discard(env, part, resume_path);
                return DL_ERR_CHECKSUM;
            }
            if (vr != DL_OK)
                return vr;
            {
                DlError se = stop_now(env);
                if (se != DL_OK)
                    return se;
            }
            if (env->progress)
                env->progress(env->progress_ctx, spec->expected_size,
                              spec->expected_size, DL_PHASE_VERIFYING);
            err = install_part(env, spec, part, resume_path, new_path);
            if (bytes_done)
                *bytes_done = spec->expected_size;
            if (err == DL_OK && env->progress)
                env->progress(env->progress_ctx, spec->expected_size,
                              spec->expected_size, DL_PHASE_DONE);
            return err;
        }
    }

    memset(&state, 0, sizeof(state));

    /* Load a trustworthy .resume; otherwise start clean. */
    {
        uint32_t resume_size = 0;
        r = st_size(env, resume_path, &resume_size);
        if (r < 0)
            return DL_ERR_STORAGE;
        if (r == 0) {
            char text[DL_RESUME_READ_MAX];
            void *f = NULL;
            int n;

            if (resume_size >= sizeof(text))
                resume_size = 0;
            if (resume_size > 0 &&
                env->storage.open(env->storage.ctx, resume_path,
                                  DL_OPEN_READ, &f) == 0) {
                n = env->storage.read(f, text, (int)resume_size);
                env->storage.close(f);
                if (n == (int)resume_size &&
                    dl_resume_parse(text, n, &state) == 0)
                    have_state = 1;
            }
        }
    }

    r = st_size(env, part, &part_size);
    if (r < 0)
        return DL_ERR_STORAGE;
    if (r == 1)
        part_size = 0;
    {
        DlError se = stop_now(env);
        if (se != DL_OK)
            return se;
    }

    if (have_state) {
        DlResumeResult ur = dl_resume_usable(&state, spec->expected_size,
                                             spec->expected_sha256, part_size);
        if (ur == DL_RESUME_COMPLETE) {
            offset = state.size;
            done = state.size;
        } else if (ur == DL_RESUME_OK) {
            offset = state.offset;
            done = state.offset;
        } else {
            discard(env, part, resume_path);
            memset(&state, 0, sizeof(state));
            have_state = 0;
            offset = 0;
            done = 0;
            part_size = 0;
        }
    } else {
        if (part_size > 0)
            (void)st_remove_ignore(env, part);
        part_size = 0;
        offset = 0;
    }

    if (done == spec->expected_size) {
        DlError vr = verify_file(env, part, spec->expected_sha256);
        if (vr == DL_ERR_CHECKSUM) {
            discard(env, part, resume_path);
            return DL_ERR_CHECKSUM;
        }
        if (vr != DL_OK)
            return vr;
        if (env->progress)
            env->progress(env->progress_ctx, done, spec->expected_size,
                          DL_PHASE_VERIFYING);
        err = install_part(env, spec, part, resume_path, new_path);
        if (bytes_done)
            *bytes_done = done;
        if (err == DL_OK && env->progress)
            env->progress(env->progress_ctx, done, spec->expected_size,
                          DL_PHASE_DONE);
        return err;
    }

    /* Free-space check (unknown free space skips the pre-check). */
    r = env->storage.free_bytes(env->storage.ctx, &free_bytes);
    if (r == 0 && free_bytes != UINT32_MAX &&
        free_bytes < (spec->expected_size - done) + DL_FREE_MARGIN_BYTES)
        return DL_ERR_NO_SPACE;

    if (done > 0 && !dl_resume_has_validator(&state)) {
        (void)st_remove_ignore(env, part);
        done = 0;
        offset = 0;
        have_state = 0;
    }

    if (env->progress)
        env->progress(env->progress_ctx, done, spec->expected_size,
                      DL_PHASE_CONNECTING);

    r = env->transport.open(env->transport.ctx, spec->url, done > 0, offset,
                            have_state ? (state.etag[0] ? state.etag
                                                        : state.last_modified)
                                       : NULL,
                            &resp, &net_err);
    if (r < 0)
        return (DlError)(net_err ? net_err : DL_ERR_NET);
    transport_open = 1;

    if (env->should_stop) {
        int stop = env->should_stop(env->stop_ctx);
        if (stop != 0) {
            env->transport.close(env->transport.ctx);
            return (DlError)stop;
        }
    }

    if (resp.chunked || resp.encoded) {
        env->transport.close(env->transport.ctx);
        discard(env, part, resume_path);
        return DL_ERR_BAD_RESPONSE;
    }

    if (resp.status == 416) {
        env->transport.close(env->transport.ctx);
        discard(env, part, resume_path);
        return DL_ERR_RANGE_REJECTED;
    }

    if (resp.status == 206) {
        if (done == 0 || !resp.has_content_range ||
            resp.range_start != offset ||
            resp.range_total != spec->expected_size ||
            resp.range_end >= resp.range_total ||
            (resp.has_content_length &&
             resp.content_length != resp.range_end - resp.range_start + 1)) {
            env->transport.close(env->transport.ctx);
            discard(env, part, resume_path);
            return DL_ERR_BAD_RESPONSE;
        }
        if (resp.has_etag && resp.etag_strong && state.etag[0] &&
            !ci_equal(resp.etag, state.etag)) {
            env->transport.close(env->transport.ctx);
            discard(env, part, resume_path);
            return DL_ERR_BAD_RESPONSE;
        }
        if (env->should_stop) {
            int stop = env->should_stop(env->stop_ctx);
            if (stop != 0) {
                env->transport.close(env->transport.ctx);
                return (DlError)stop;
            }
        }
        if (env->storage.open(env->storage.ctx, part, DL_OPEN_APPEND,
                              &part_file) != 0 ||
            env->storage.seek(part_file, offset) != 0) {
            if (part_file)
                env->storage.close(part_file);
            env->transport.close(env->transport.ctx);
            return DL_ERR_STORAGE;
        }
        done = offset;
        last_checkpoint = done;
    } else if (resp.status == 200) {
        if (!resp.has_content_length ||
            resp.content_length != spec->expected_size) {
            env->transport.close(env->transport.ctx);
            discard(env, part, resume_path);
            return DL_ERR_BAD_RESPONSE;
        }
        memset(&state, 0, sizeof(state));
        state.size = spec->expected_size;
        snprintf(state.sha256, sizeof(state.sha256), "%s",
                 spec->expected_sha256);
        if (resp.has_etag && resp.etag_strong)
            snprintf(state.etag, sizeof(state.etag), "%s", resp.etag);
        else if (resp.has_last_modified)
            snprintf(state.last_modified, sizeof(state.last_modified), "%s",
                     resp.last_modified);
        state.offset = 0;
        if (save_resume(env, &state, resume_path) != 0) {
            env->transport.close(env->transport.ctx);
            return DL_ERR_STORAGE;
        }
        have_state = 1;
        if (env->storage.open(env->storage.ctx, part, DL_OPEN_WRITE,
                              &part_file) != 0) {
            env->transport.close(env->transport.ctx);
            return DL_ERR_STORAGE;
        }
        done = 0;
        offset = 0;
        last_checkpoint = 0;
    } else {
        env->transport.close(env->transport.ctx);
        return DL_ERR_HTTP_STATUS;
    }

    if (!have_state) {
        memset(&state, 0, sizeof(state));
        state.size = spec->expected_size;
        snprintf(state.sha256, sizeof(state.sha256), "%s",
                 spec->expected_sha256);
        state.offset = done;
    }

    for (;;) {
        int stop;
        int n;

        if (env->should_stop) {
            stop = env->should_stop(env->stop_ctx);
            if (stop != 0) {
                err = (DlError)stop;
                if (err == DL_ERR_SUSPENDED) {
                    env->storage.close(part_file);
                    env->transport.close(env->transport.ctx);
                    if (bytes_done)
                        *bytes_done = done;
                    return err;
                }
                if (used > 0) {
                    if (env->storage.write(part_file, env->buffer, (int)used) !=
                        (int)used) {
                        err = DL_ERR_STORAGE;
                        break;
                    }
                    used = 0;
                }
                if (env->storage.sync(part_file) != 0) {
                    err = DL_ERR_STORAGE;
                    break;
                }
                state.offset = done;
                if (save_resume(env, &state, resume_path) != 0) {
                    err = DL_ERR_STORAGE;
                    break;
                }
                env->storage.close(part_file);
                env->transport.close(env->transport.ctx);
                if (bytes_done)
                    *bytes_done = done;
                return err;
            }
        }

        if (used >= env->buffer_size) {
            if (env->storage.write(part_file, env->buffer, (int)used) !=
                (int)used) {
                err = DL_ERR_STORAGE;
                break;
            }
            used = 0;
        }

        n = env->transport.read(env->transport.ctx,
                                env->buffer + used,
                                (int)(env->buffer_size - used), &net_err);
        if (n < 0) {
            err = (DlError)(net_err ? net_err : DL_ERR_NET);
            if (err == DL_ERR_SUSPENDED) {
                env->storage.close(part_file);
                env->transport.close(env->transport.ctx);
                if (bytes_done)
                    *bytes_done = done;
                return err;
            }
            goto flush_retry;
        }
        if (n == 0) {
            if (done != spec->expected_size) {
                err = DL_ERR_NET;
                goto flush_retry;
            }
            break;
        }
        done += (uint32_t)n;
        used += (uint32_t)n;
        if (env->attempt_bytes)
            *env->attempt_bytes += (uint32_t)n;
        if (done > spec->expected_size) {
            err = DL_ERR_BAD_RESPONSE;
            goto discard_and_fail;
        }
        if (env->progress)
            env->progress(env->progress_ctx, done, spec->expected_size,
                          DL_PHASE_RECEIVING);
        if (done - last_checkpoint >= DL_CHECKPOINT_BYTES) {
            if (env->storage.write(part_file, env->buffer, (int)used) !=
                (int)used) {
                err = DL_ERR_STORAGE;
                break;
            }
            used = 0;
            if (env->storage.sync(part_file) != 0) {
                err = DL_ERR_STORAGE;
                break;
            }
            state.offset = done;
            if (save_resume(env, &state, resume_path) != 0) {
                err = DL_ERR_STORAGE;
                break;
            }
            last_checkpoint = done;
        }
    }

    if (err == DL_ERR_STORAGE)
        goto close_and_fail;

    /* Final flush and durable checkpoint at size. */
    if (used > 0) {
        if (env->storage.write(part_file, env->buffer, (int)used) != (int)used) {
            err = DL_ERR_STORAGE;
            goto close_and_fail;
        }
        used = 0;
    }
    if (env->storage.sync(part_file) != 0) {
        err = DL_ERR_STORAGE;
        goto close_and_fail;
    }
    state.offset = done;
    if (save_resume(env, &state, resume_path) != 0) {
        err = DL_ERR_STORAGE;
        goto close_and_fail;
    }
    env->storage.close(part_file);
    part_file = NULL;
    env->transport.close(env->transport.ctx);
    transport_open = 0;

    if (env->progress)
        env->progress(env->progress_ctx, done, spec->expected_size,
                      DL_PHASE_VERIFYING);
    {
        DlError vr = verify_file(env, part, spec->expected_sha256);
        if (vr == DL_ERR_CHECKSUM) {
            discard(env, part, resume_path);
            if (bytes_done)
                *bytes_done = done;
            return DL_ERR_CHECKSUM;
        }
        if (vr != DL_OK) {
            if (bytes_done)
                *bytes_done = done;
            return vr;
        }
    }
    {
        DlError se = stop_now(env);
        if (se != DL_OK) {
            if (bytes_done)
                *bytes_done = done;
            return se;
        }
    }
    if (env->progress)
        env->progress(env->progress_ctx, done, spec->expected_size,
                      DL_PHASE_INSTALLING);
    err = install_part(env, spec, part, resume_path, new_path);
    if (bytes_done)
        *bytes_done = done;
    if (err == DL_OK && env->progress)
        env->progress(env->progress_ctx, done, spec->expected_size,
                      DL_PHASE_DONE);
    return err;

flush_retry:
    if (used > 0) {
        if (env->storage.write(part_file, env->buffer, (int)used) != (int)used) {
            err = DL_ERR_STORAGE;
            goto close_and_fail;
        }
        used = 0;
    }
    if (env->storage.sync(part_file) != 0) {
        err = DL_ERR_STORAGE;
        goto close_and_fail;
    }
    state.offset = done;
    if (save_resume(env, &state, resume_path) != 0) {
        err = DL_ERR_STORAGE;
        goto close_and_fail;
    }
    env->storage.close(part_file);
    env->transport.close(env->transport.ctx);
    if (bytes_done)
        *bytes_done = done;
    return err;

discard_and_fail:
    env->storage.close(part_file);
    if (transport_open)
        env->transport.close(env->transport.ctx);
    discard(env, part, resume_path);
    if (bytes_done)
        *bytes_done = done;
    return err;

close_and_fail:
    if (part_file)
        env->storage.close(part_file);
    if (transport_open)
        env->transport.close(env->transport.ctx);
    if (bytes_done)
        *bytes_done = done;
    return err;
}
