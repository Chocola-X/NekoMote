/*
 * nekomote_abi_host — C ABI wallpaper verification platform.
 *
 * Pure C (no C++ engine calls). Verifies the C ABI is complete enough to
 * implement all wallpaper features: multi-model directory scan, character
 * cross-fade switching, background loading and fade switching, mirror,
 * mouse gaze, touch, pause, reset, fit.
 *
 * Usage: nekomote_abi_host --assets <dir> [--character <name>] [--width N] [--height N]
 *        nekomote_abi_host <model.nkm.json> <session.nks.json> [options]
 *
 * Keys: Tab=next char, 1-9=select char, B/N=next bg, C=clear bg,
 *       Space=pause, R=reset, F=fit, Esc=quit
 */

#include <nekomote/abi.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <dirent.h>
#include <math.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define MAX_CHARACTERS 512
#define MAX_BACKGROUNDS 64
#define MAX_NAME 256
#define MAX_PATH 1024
#define FADE_CHAR_SECONDS 0.4
#define FADE_BG_SECONDS 0.8
#define MAX_CLIPS 32
#define AUDIO_RMS_FRAMES 512
#define AUDIO_CMD_QUEUE 32

/* ------------------------------------------------------------------ */
/* Audio (PulseAudio pa_simple + worker thread)                        */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t sample_rate;
    uint32_t channels;
    float *samples;
    size_t sample_count;
    float *rms;
    size_t rms_count;
} AudioClip;

typedef struct {
    nekomote_audio_command commands[AUDIO_CMD_QUEUE];
    int cmd_read;
    int cmd_write;
    pthread_mutex_t mutex;
    pthread_t thread;
    pa_simple *stream;
    AudioClip clips[MAX_CLIPS];
    int clip_count;
    int playing;
    int paused;
    int current_clip;
    size_t position;
    float current_rms;
    int quitting;
    int started;
} AudioDevice;

static int load_wav(const char *path, AudioClip *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char header[12];
    if (fread(header, 1, 12, f) != 12 || memcmp(header, "RIFF", 4) != 0 ||
        memcmp(header + 8, "WAVE", 4) != 0)
    { fclose(f); return 0; }
    uint16_t encoding = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0;
    unsigned char *data = NULL;
    size_t data_size = 0;
    for (;;)
    {
        unsigned char chunk_hdr[8];
        size_t n = fread(chunk_hdr, 1, 8, f);
        if (n != 8) break;
        uint32_t len = chunk_hdr[4] | (chunk_hdr[5] << 8) |
                       (chunk_hdr[6] << 16) | ((uint32_t)chunk_hdr[7] << 24);
        if (memcmp(chunk_hdr, "fmt ", 4) == 0)
        {
            unsigned char fmt[16];
            if (len < 16 || fread(fmt, 1, 16, f) != 16) { fclose(f); free(data); return 0; }
            encoding = fmt[0] | (fmt[1] << 8);
            channels = fmt[2] | (fmt[3] << 8);
            sample_rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            bits = fmt[14] | (fmt[15] << 8);
            if (len > 16) fseek(f, (long)(len - 16), SEEK_CUR);
        }
        else if (memcmp(chunk_hdr, "data", 4) == 0)
        {
            data_size = len;
            data = malloc(data_size);
            if (!data || fread(data, 1, data_size, f) != data_size)
            { fclose(f); free(data); return 0; }
        }
        else
            fseek(f, (long)len, SEEK_CUR);
        if (len & 1) fseek(f, 1, SEEK_CUR);
    }
    fclose(f);
    if (!data || encoding != 1 || (bits != 16 && bits != 8) ||
        channels < 1 || channels > 2 || sample_rate < 8000 || sample_rate > 192000)
    { free(data); return 0; }
    size_t bytes = bits / 8;
    size_t sample_count = data_size / bytes;
    out->sample_rate = sample_rate;
    out->channels = channels;
    out->samples = malloc(sample_count * sizeof(float));
    if (!out->samples) { free(data); return 0; }
    for (size_t i = 0; i < sample_count; i++)
    {
        if (bits == 16)
        {
            int16_t v = (int16_t)(data[i * 2] | (data[i * 2 + 1] << 8));
            out->samples[i] = (float)v / 32768.0f;
        }
        else
            out->samples[i] = ((float)data[i] - 128.0f) / 128.0f;
    }
    out->sample_count = sample_count;
    free(data);
    size_t frames = sample_count / channels;
    out->rms_count = (frames + AUDIO_RMS_FRAMES - 1) / AUDIO_RMS_FRAMES;
    out->rms = malloc(out->rms_count * sizeof(float));
    if (!out->rms) { free(out->samples); return 0; }
    for (size_t w = 0; w < out->rms_count; w++)
    {
        size_t begin = w * AUDIO_RMS_FRAMES;
        size_t end = frames < begin + AUDIO_RMS_FRAMES ? frames : begin + AUDIO_RMS_FRAMES;
        double sq = 0;
        for (size_t fr = begin; fr < end; fr++)
        {
            double mono = 0;
            for (uint32_t c = 0; c < channels; c++)
                mono += (double)out->samples[fr * channels + c];
            mono /= channels;
            sq += mono * mono;
        }
        out->rms[w] = (float)sqrt(sq / (double)(end - begin));
    }
    return 1;
}

static void *audio_worker(void *arg)
{
    AudioDevice *dev = (AudioDevice *)arg;
    while (!dev->quitting)
    {
        /* Process one command (PulseAudio calls outside mutex) */
        pthread_mutex_lock(&dev->mutex);
        int has_cmd = dev->cmd_read != dev->cmd_write;
        nekomote_audio_command cmd = {0};
        if (has_cmd)
        {
            cmd = dev->commands[dev->cmd_read % AUDIO_CMD_QUEUE];
            dev->cmd_read++;
        }
        pthread_mutex_unlock(&dev->mutex);
        if (has_cmd)
        {
            if (cmd.kind == NEKOMOTE_AUDIO_STOP)
            {
                pthread_mutex_lock(&dev->mutex);
                pa_simple *s = dev->stream;
                dev->stream = NULL;
                dev->playing = 0;
                dev->current_clip = -1;
                dev->current_rms = 0;
                pthread_mutex_unlock(&dev->mutex);
                if (s) { pa_simple_drain(s, NULL); pa_simple_free(s); }
            }
            else if (cmd.kind == NEKOMOTE_AUDIO_PLAY && cmd.clip_id > 0 &&
                     (int)cmd.clip_id <= dev->clip_count)
            {
                pthread_mutex_lock(&dev->mutex);
                pa_simple *old = dev->stream;
                dev->stream = NULL;
                int clip_idx = (int)cmd.clip_id - 1;
                dev->current_clip = clip_idx;
                dev->position = 0;
                dev->playing = 1;
                uint32_t sr = dev->clips[clip_idx].sample_rate;
                uint32_t ch = dev->clips[clip_idx].channels;
                pthread_mutex_unlock(&dev->mutex);
                if (old) pa_simple_free(old);
                pa_sample_spec spec = {PA_SAMPLE_FLOAT32NE, sr, (uint8_t)ch};
                uint32_t frame_bytes = ch * (uint32_t)sizeof(float);
                uint32_t blen = sr * frame_bytes / 5;
                pa_buffer_attr attr = {blen * 2, blen, 0, 512 * frame_bytes, 0};
                int err = 0;
                pa_simple *s = pa_simple_new(NULL, "NekoMote ABI Host",
                    PA_STREAM_PLAYBACK, NULL, "voice", &spec, NULL, &attr, &err);
                pthread_mutex_lock(&dev->mutex);
                dev->stream = s;
                if (!s) dev->playing = 0;
                pthread_mutex_unlock(&dev->mutex);
            }
            else if (cmd.kind == NEKOMOTE_AUDIO_PAUSE)
            {
                pthread_mutex_lock(&dev->mutex);
                dev->paused = 1;
                pthread_mutex_unlock(&dev->mutex);
            }
            else if (cmd.kind == NEKOMOTE_AUDIO_RESUME)
            {
                pthread_mutex_lock(&dev->mutex);
                dev->paused = 0;
                pthread_mutex_unlock(&dev->mutex);
            }
        }

        /* Play one chunk (pa_simple_write outside mutex) */
        pthread_mutex_lock(&dev->mutex);
        int should_play = dev->playing && !dev->paused && dev->stream &&
                          dev->current_clip >= 0;
        pa_simple *stream = NULL;
        float *write_ptr = NULL;
        size_t chunk = 0, channels = 0, rms_count = 0, sample_rate = 0;
        float *rms = NULL;
        int drain = 0;
        if (should_play)
        {
            AudioClip *clip = &dev->clips[dev->current_clip];
            size_t remaining = clip->sample_count - dev->position;
            chunk = remaining < 2048 * clip->channels ? remaining : 2048 * clip->channels;
            stream = dev->stream;
            if (chunk > 0)
            {
                write_ptr = clip->samples + dev->position;
                channels = clip->channels;
                rms_count = clip->rms_count;
                rms = clip->rms;
                sample_rate = clip->sample_rate;
            }
            else
                drain = 1;
        }
        pthread_mutex_unlock(&dev->mutex);

        if (chunk > 0 && stream)
        {
            int err = 0;
            pa_simple_write(stream, write_ptr, chunk * sizeof(float), &err);
            pthread_mutex_lock(&dev->mutex);
            dev->position += chunk;
            size_t frame = dev->position / channels;
            pa_usec_t latency = pa_simple_get_latency(stream, &err);
            if (latency != (pa_usec_t)-1)
            {
                size_t lat_frames = (size_t)((double)latency *
                                              (double)sample_rate / 1000000.0);
                frame = frame > lat_frames ? frame - lat_frames : 0;
            }
            size_t idx = frame / AUDIO_RMS_FRAMES;
            if (idx < rms_count)
                dev->current_rms = rms[idx];
            pthread_mutex_unlock(&dev->mutex);
        }
        else if (drain && stream)
        {
            pa_simple_drain(stream, NULL);
            pthread_mutex_lock(&dev->mutex);
            if (dev->stream) { pa_simple_free(dev->stream); dev->stream = NULL; }
            dev->playing = 0;
            dev->current_clip = -1;
            dev->current_rms = 0;
            pthread_mutex_unlock(&dev->mutex);
        }

        usleep(4000);
    }
    pthread_mutex_lock(&dev->mutex);
    pa_simple *s = dev->stream;
    dev->stream = NULL;
    pthread_mutex_unlock(&dev->mutex);
    if (s) { pa_simple_drain(s, NULL); pa_simple_free(s); }
    return NULL;
}

static void audio_init(AudioDevice *dev)
{
    memset(dev, 0, sizeof(*dev));
    pthread_mutex_init(&dev->mutex, NULL);
    dev->current_clip = -1;
}

static void audio_start(AudioDevice *dev)
{
    dev->quitting = 0;
    pthread_create(&dev->thread, NULL, audio_worker, dev);
    dev->started = 1;
}

static void audio_submit(AudioDevice *dev, nekomote_audio_command cmd)
{
    pthread_mutex_lock(&dev->mutex);
    if (dev->cmd_write - dev->cmd_read < AUDIO_CMD_QUEUE)
    {
        dev->commands[dev->cmd_write % AUDIO_CMD_QUEUE] = cmd;
        dev->cmd_write++;
    }
    pthread_mutex_unlock(&dev->mutex);
}

static float audio_rms(AudioDevice *dev)
{
    pthread_mutex_lock(&dev->mutex);
    float rms = dev->playing && !dev->paused ? dev->current_rms : 0;
    pthread_mutex_unlock(&dev->mutex);
    return rms;
}

static void audio_destroy(AudioDevice *dev)
{
    if (!dev->started) return;
    dev->quitting = 1;
    pthread_join(dev->thread, NULL);
    pthread_mutex_destroy(&dev->mutex);
    for (int i = 0; i < dev->clip_count; i++)
    {
        free(dev->clips[i].samples);
        free(dev->clips[i].rms);
    }
    dev->started = 0;
}

/* ------------------------------------------------------------------ */
/* Catalog                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[MAX_NAME];
    char model_path[MAX_PATH];
    char session_path[MAX_PATH];
    char directory[MAX_PATH];
    float cutoff_y;
    int has_cutoff;
} CharacterEntry;

typedef struct {
    char name[MAX_NAME];
    char path[MAX_PATH];
} BackgroundEntry;

typedef struct {
    CharacterEntry characters[MAX_CHARACTERS];
    int character_count;
    BackgroundEntry backgrounds[MAX_BACKGROUNDS];
    int background_count;
} Catalog;

static int has_file(const char *dir, const char *name)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int read_cutoff_y(const char *dir, float *out)
{
    char meta_path[MAX_PATH];
    snprintf(meta_path, sizeof(meta_path), "%s/meta.json", dir);
    FILE *f = fopen(meta_path, "r");
    if (!f) return 0;
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    const char *p = strstr(buf, "cutoff_y");
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    float val;
    if (sscanf(p, " %f", &val) != 1) return 0;
    *out = val;
    return 1;
}

static int char_cmp(const void *a, const void *b)
{
    return strcmp(((const CharacterEntry *)a)->name,
                  ((const CharacterEntry *)b)->name);
}

static int bg_cmp(const void *a, const void *b)
{
    return strcmp(((const BackgroundEntry *)a)->name,
                  ((const BackgroundEntry *)b)->name);
}

static void scan_catalog(Catalog *cat, const char *assets_dir)
{
    cat->character_count = 0;
    cat->background_count = 0;

    char chars_dir[MAX_PATH];
    snprintf(chars_dir, sizeof(chars_dir), "%s/characters", assets_dir);
    DIR *d = opendir(chars_dir);
    if (d)
    {
        struct dirent *e;
        while ((e = readdir(d)) && cat->character_count < MAX_CHARACTERS)
        {
            if (e->d_name[0] == '.')
                continue;
            char subdir[MAX_PATH];
            snprintf(subdir, sizeof(subdir), "%s/%s", chars_dir, e->d_name);
            struct stat st;
            if (stat(subdir, &st) != 0 || !S_ISDIR(st.st_mode))
                continue;
            if (!has_file(subdir, "model.nkm.json") || !has_file(subdir, "session.nks.json"))
                continue;
            CharacterEntry *c = &cat->characters[cat->character_count++];
            strncpy(c->name, e->d_name, MAX_NAME - 1);
            c->name[MAX_NAME - 1] = '\0';
            strncpy(c->directory, subdir, MAX_PATH - 1);
            c->directory[MAX_PATH - 1] = '\0';
            snprintf(c->model_path, MAX_PATH, "%s/model.nkm.json", subdir);
            snprintf(c->session_path, MAX_PATH, "%s/session.nks.json", subdir);
            c->cutoff_y = 0;
            c->has_cutoff = 0;
            float cutoff;
            if (read_cutoff_y(subdir, &cutoff))
            {
                c->cutoff_y = cutoff;
                c->has_cutoff = 1;
            }
        }
        closedir(d);
    }
    qsort(cat->characters, (size_t)cat->character_count, sizeof(CharacterEntry), char_cmp);

    char bg_dir[MAX_PATH];
    snprintf(bg_dir, sizeof(bg_dir), "%s/backgrounds", assets_dir);
    d = opendir(bg_dir);
    if (d)
    {
        struct dirent *e;
        while ((e = readdir(d)) && cat->background_count < MAX_BACKGROUNDS)
        {
            if (e->d_name[0] == '.')
                continue;
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || (strcmp(dot, ".png") != 0 && strcmp(dot, ".PNG") != 0))
                continue;
            BackgroundEntry *b = &cat->backgrounds[cat->background_count++];
            size_t stem_len = (size_t)(dot - e->d_name);
            if (stem_len >= MAX_NAME) stem_len = MAX_NAME - 1;
            memcpy(b->name, e->d_name, stem_len);
            b->name[stem_len] = '\0';
            snprintf(b->path, MAX_PATH, "%s/%s", bg_dir, e->d_name);
        }
        closedir(d);
    }
    qsort(cat->backgrounds, (size_t)cat->background_count, sizeof(BackgroundEntry), bg_cmp);
}

/* ------------------------------------------------------------------ */
/* Character holder                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    nekomote_model *model;
    nekomote_session_asset *session_asset;
    nekomote_program *program;
    nekomote_session *session;
    nekomote_renderer *renderer;
    int mirror;
    float cutoff_y;
    int has_cutoff;
    AudioDevice audio;
    int has_audio;
    char name[MAX_NAME];
} Holder;

static void check_renderer(const char *where, nekomote_error e)
{
    if (e != NEKOMOTE_OK)
        fprintf(stderr, "[abi_host] %s: error %d (%s)\n", where, (int)e,
                nekomote_error_message() ? nekomote_error_message() : "unknown");
}

static int load_textures(Holder *h)
{
    size_t tex_count = nekomote_program_texture_count(h->program);
    nekomote_renderer_capacity cap = {0};
    cap.vertices = 65536;
    cap.indices = 131072;
    cap.commands = 1024;
    cap.textures = tex_count > 0 ? tex_count : 1;
    cap.mask_commands = 256;
    cap.masks = 64;
    nekomote_renderer_reserve(h->renderer, cap);
    (void)nekomote_last_error();

    const char *model_path = h->name;
    (void)model_path;
    int uploaded = 0;
    for (size_t i = 0; i < tex_count; i++)
    {
        nekomote_texture_info tex = nekomote_program_texture(h->program, i);
        if (!tex.path || tex.id == 0)
            continue;
        char full_path[MAX_PATH];
        const char *slash = strrchr(h->name, '/');
        (void)slash;
        snprintf(full_path, sizeof(full_path), "%s/textures/%s",
                 nekomote_program_texture(h->program, 0).path ? "." : ".",
                 tex.path);
        /* Try relative to model directory */
        snprintf(full_path, sizeof(full_path), "%s/%s",
                 h->name, tex.path);
        nekomote_image *img = nekomote_image_load_png(full_path);
        if (!img)
            continue;
        nekomote_error ue = nekomote_renderer_upload_texture(
            h->renderer, tex.id,
            nekomote_image_width(img), nekomote_image_height(img),
            nekomote_image_pixels(img));
        check_renderer("upload_texture", ue);
        nekomote_image_destroy(img);
        if (ue == NEKOMOTE_OK)
            uploaded++;
    }
    return uploaded;
}

static int load_holder(Holder *h, const CharacterEntry *entry)
{
    memset(h, 0, sizeof(*h));
    strncpy(h->name, entry->directory, MAX_NAME - 1);
    h->name[MAX_NAME - 1] = '\0';

    h->model = nekomote_model_load(entry->model_path);
    if (!h->model) return 0;
    h->session_asset = nekomote_session_asset_load(entry->session_path);
    if (!h->session_asset) { nekomote_model_destroy(h->model); return 0; }
    h->program = nekomote_program_compile(h->model, h->session_asset);
    if (!h->program) { nekomote_session_asset_destroy(h->session_asset); nekomote_model_destroy(h->model); return 0; }

    h->has_audio = 0;
    h->mirror = nekomote_program_mirror(h->program);
    h->cutoff_y = entry->cutoff_y;
    h->has_cutoff = entry->has_cutoff;

    size_t audio_count = nekomote_program_audio_count(h->program);
    if (audio_count > 0 && audio_count <= MAX_CLIPS)
    {
        audio_init(&h->audio);
        const char *slash = strrchr(entry->session_path, '/');
        size_t dir_len = slash ? (size_t)(slash - entry->session_path) : 0;
        for (size_t i = 0; i < audio_count; i++)
        {
            const char *ap = nekomote_program_audio_path(h->program, i);
            char full[MAX_PATH];
            if (dir_len > 0)
                snprintf(full, sizeof(full), "%.*s/%s", (int)dir_len, entry->session_path, ap);
            else
                snprintf(full, sizeof(full), "%s", ap);
            if (load_wav(full, &h->audio.clips[h->audio.clip_count]))
                h->audio.clip_count++;
        }
        if (h->audio.clip_count > 0)
        {
            audio_start(&h->audio);
            h->has_audio = 1;
        }
    }

    nekomote_session_config config = {0};
    config.random_seed = 42;
    config.pointer = NEKOMOTE_POINTER_LOCAL_SURFACE;
    config.audio = h->has_audio ? NEKOMOTE_AUDIO_DEVICE : NEKOMOTE_AUDIO_DISABLED;
    h->session = nekomote_session_create(h->program, config);
    if (!h->session) { if (h->has_audio) audio_destroy(&h->audio); nekomote_program_destroy(h->program); nekomote_session_asset_destroy(h->session_asset); nekomote_model_destroy(h->model); return 0; }

    nekomote_session_start(h->session);
    return 1;
}

static void load_holder_with_gl(Holder *h, const CharacterEntry *entry)
{
    if (!load_holder(h, entry))
    {
        fprintf(stderr, "Error: failed to load character %s\n", entry->name);
        return;
    }
    h->renderer = nekomote_renderer_create(glfwGetProcAddress);
    load_textures(h);
}

static void destroy_holder(Holder *h)
{
    if (h->has_audio) audio_destroy(&h->audio);
    if (h->renderer) nekomote_renderer_destroy(h->renderer);
    if (h->session) nekomote_session_destroy(h->session);
    if (h->program) nekomote_program_destroy(h->program);
    if (h->session_asset) nekomote_session_asset_destroy(h->session_asset);
    if (h->model) nekomote_model_destroy(h->model);
    memset(h, 0, sizeof(*h));
}

/* ------------------------------------------------------------------ */
/* Background state                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    nekomote_renderer *renderer;
    int has_background;
    char current_path[MAX_PATH];
} BackgroundState;

static void apply_background(BackgroundState *bg, nekomote_session *session,
                             const char *bg_path, double fade_duration)
{
    nekomote_image *new_img = nekomote_image_load_png(bg_path);
    if (!new_img)
    {
        fprintf(stderr, "  Warning: failed to load background %s\n", bg_path);
        return;
    }
    int w = nekomote_image_width(new_img);
    int h = nekomote_image_height(new_img);

    if (!bg->has_background || fade_duration <= 0)
    {
        if (bg->renderer) nekomote_renderer_destroy(bg->renderer);
        bg->renderer = nekomote_renderer_create(glfwGetProcAddress);
        nekomote_renderer_capacity cap = {0};
        cap.vertices = 8; cap.indices = 12; cap.commands = 2; cap.textures = 2;
        check_renderer("bg reserve", nekomote_renderer_reserve(bg->renderer, cap));
        check_renderer("bg upload", nekomote_renderer_upload_texture(bg->renderer, 1, w, h, nekomote_image_pixels(new_img)));
        nekomote_session_set_background(session, 1, w, h);
        bg->has_background = 1;
        strncpy(bg->current_path, bg_path, MAX_PATH - 1);
        bg->current_path[MAX_PATH - 1] = '\0';
    }
    else
    {
        nekomote_image *old_img = nekomote_image_load_png(bg->current_path);
        if (!old_img)
        {
            nekomote_image_destroy(new_img);
            return;
        }
        if (bg->renderer) nekomote_renderer_destroy(bg->renderer);
        bg->renderer = nekomote_renderer_create(glfwGetProcAddress);
        nekomote_renderer_capacity cap = {0};
        cap.vertices = 8; cap.indices = 12; cap.commands = 2; cap.textures = 2;
        check_renderer("bg reserve", nekomote_renderer_reserve(bg->renderer, cap));
        check_renderer("bg upload old", nekomote_renderer_upload_texture(bg->renderer, 1, w, h, nekomote_image_pixels(old_img)));
        check_renderer("bg upload new", nekomote_renderer_upload_texture(bg->renderer, 2, w, h, nekomote_image_pixels(new_img)));
        nekomote_session_set_background(session, 1,
            nekomote_image_width(old_img), nekomote_image_height(old_img));
        nekomote_session_fade_background(session, 2, w, h, fade_duration);
        bg->has_background = 1;
        strncpy(bg->current_path, bg_path, MAX_PATH - 1);
        bg->current_path[MAX_PATH - 1] = '\0';
        nekomote_image_destroy(old_img);
    }
    nekomote_image_destroy(new_img);
}

static void clear_background(BackgroundState *bg, nekomote_session *session)
{
    nekomote_session_clear_background(session);
    bg->has_background = 0;
    bg->current_path[0] = '\0';
    if (bg->renderer)
    {
        nekomote_renderer_destroy(bg->renderer);
        bg->renderer = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* App state                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    FADE_NONE,
    FADE_PENDING,
    FADE_CROSSFADE,
} FadeState;

typedef struct {
    Catalog catalog;
    Holder current;
    Holder old;
    BackgroundState bg;
    int has_old;
    int char_index;
    int bg_index;
    FadeState fade_state;
    double fade_start;
    float fade_alpha;
    int need_bg_switch;
    int target_bg_index;
    int need_bg_clear;
    int width;
    int height;
    float origin_x;
    float origin_y;
    float scale;
    int paused;
    int fit;
} App;

static App g_app;

/* ------------------------------------------------------------------ */
/* Rendering                                                          */
/* ------------------------------------------------------------------ */

static void render_holder(Holder *h, nekomote_render_target target,
                          float alpha, int clear_target)
{
    if (!h->session || !h->renderer)
        return;
    nekomote_render_frame frame = nekomote_session_frame(h->session);
    check_renderer("prepare", nekomote_renderer_prepare(h->renderer, frame));
    check_renderer("resize_output", nekomote_renderer_resize_output(h->renderer, target.width, target.height));

    nekomote_render_view view = {0};
    view.origin_x = (float)target.width * 0.5f;
    view.origin_y = (float)target.height * 0.5f;
    view.scale = 1.0f;
    view.clear[0] = 0.06f;
    view.clear[1] = 0.07f;
    view.clear[2] = 0.10f;
    view.clear[3] = 1.0f;
    view.alpha = alpha;
    view.flip_x = h->mirror;
    check_renderer("render", nekomote_renderer_render(h->renderer, target, view,
                             clear_target ? NEKOMOTE_TARGET_CLEAR : NEKOMOTE_TARGET_PRESERVE));
}

static void fit_character(void)
{
    if (!g_app.current.session)
        return;
    int w = g_app.width, h = g_app.height;
    if (w <= 32 || h <= 32)
        return;
    nekomote_session_prepare_frame(g_app.current.session);
    nekomote_render_frame frame = nekomote_session_frame(g_app.current.session);
    float left = 1e30f, top = 1e30f, right = -1e30f, bottom = -1e30f;
    for (size_t c = 0; c < frame.command_count; c++)
    {
        for (uint32_t i = 0; i < frame.commands[c].index_count; i++)
        {
            const nekomote_vertex *v =
                &frame.vertices[frame.indices[frame.commands[c].first_index + i]];
            if (v->color[3] <= 0)
                continue;
            if (v->x < left) left = v->x;
            if (v->x > right) right = v->x;
            if (v->y < top) top = v->y;
            if (v->y > bottom) bottom = v->y;
        }
    }
    if (!(right > left) || !(bottom > top))
        return;
    if (g_app.current.has_cutoff)
    {
        float effective_bottom = g_app.current.cutoff_y;
        if (!(effective_bottom > top))
            return;
        float floor_val = (float)h + 40.0f;
        float ratio = fminf((float)(w - 32) / (right - left),
                            floor_val / (effective_bottom - top));
        float new_scale = g_app.scale * ratio / 1.21f;
        if (new_scale < 0.05f) new_scale = 0.05f;
        if (new_scale > 5.0f) new_scale = 5.0f;
        float actual_ratio = new_scale / g_app.scale;
        g_app.origin_x = (g_app.origin_x - (left + right) * 0.5f) * actual_ratio;
        g_app.origin_y = (float)h * 0.5f -
                         (effective_bottom - top) * new_scale * 0.5f - 100.0f;
        g_app.scale = new_scale;
    }
    else
    {
        float ratio = fminf((float)(w - 32) / (right - left),
                            (float)(h - 32) / (bottom - top));
        float new_scale = g_app.scale * ratio;
        if (new_scale < 0.05f) new_scale = 0.05f;
        if (new_scale > 5.0f) new_scale = 5.0f;
        float actual_ratio = new_scale / g_app.scale;
        g_app.origin_x = (g_app.origin_x - (left + right) * 0.5f) * actual_ratio;
        g_app.origin_y = (g_app.origin_y - (top + bottom) * 0.5f) * actual_ratio;
        g_app.scale = new_scale;
    }
    nekomote_session_set_scale(g_app.current.session, g_app.scale);
    nekomote_session_set_origin(g_app.current.session, g_app.origin_x, g_app.origin_y);
}

static void pointer_to_center(GLFWwindow *win, double x, double y,
                              float *out_x, float *out_y)
{
    int w = 0, h = 0, fw = 0, fh = 0;
    glfwGetWindowSize(win, &w, &h);
    glfwGetFramebufferSize(win, &fw, &fh);
    *out_x = (float)(w > 0 ? x * fw / w - fw * 0.5 : 0);
    *out_y = (float)(h > 0 ? y * fh / h - fh * 0.5 : 0);
}

static void render_frame(void)
{
    nekomote_render_target target = {0};
    target.width = g_app.width;
    target.height = g_app.height;

    /* Background layer */
    int has_bg = 0;
    if (g_app.bg.has_background && g_app.bg.renderer)
    {
        nekomote_render_frame bg_frame = nekomote_session_background_frame(g_app.current.session);
        if (bg_frame.command_count > 0)
        {
            check_renderer("bg prepare", nekomote_renderer_prepare(g_app.bg.renderer, bg_frame));
            check_renderer("bg resize", nekomote_renderer_resize_output(g_app.bg.renderer, target.width, target.height));
            nekomote_render_view bg_view = {0};
            bg_view.scale = 1.0f;
            bg_view.clear[3] = 1.0f;
            bg_view.alpha = 1.0f;
            check_renderer("bg render", nekomote_renderer_render(g_app.bg.renderer, target, bg_view, NEKOMOTE_TARGET_CLEAR));
            has_bg = 1;
        }
    }

    /* Character layer */
    if (g_app.fade_state == FADE_CROSSFADE && g_app.has_old)
    {
        render_holder(&g_app.old, target, 1.0f - g_app.fade_alpha, !has_bg);
        render_holder(&g_app.current, target, g_app.fade_alpha, 0);
    }
    else
    {
        float alpha = g_app.fade_state == FADE_NONE ? 1.0f : g_app.fade_alpha;
        render_holder(&g_app.current, target, alpha, !has_bg);
    }
}

/* ------------------------------------------------------------------ */
/* Switching                                                          */
/* ------------------------------------------------------------------ */

static void switch_character(int index)
{
    if (index < 0 || index >= g_app.catalog.character_count)
        return;
    if (index == g_app.char_index || g_app.fade_state != FADE_NONE)
        return;
    g_app.fade_state = FADE_PENDING;
    g_app.char_index = index;
}

static void switch_background(int index)
{
    if (index < 0 || index >= g_app.catalog.background_count)
        return;
    g_app.target_bg_index = index;
    g_app.need_bg_switch = 1;
}

/* ------------------------------------------------------------------ */
/* Input callbacks                                                    */
/* ------------------------------------------------------------------ */

static void on_resize(GLFWwindow *win, int w, int h)
{
    (void)win;
    g_app.width = w;
    g_app.height = h;
    if (g_app.current.session)
        nekomote_session_set_output_size(g_app.current.session, w, h);
}

static void on_mouse_move(GLFWwindow *win, double x, double y)
{
    (void)win;
    float px, py;
    pointer_to_center(win, x, y, &px, &py);
    if (g_app.current.mirror)
        px = -px;
    nekomote_session_pointer_move(g_app.current.session, px, py);
}

static void on_mouse_button(GLFWwindow *win, int button, int action, int mods)
{
    (void)mods;
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
        return;
    double x, y;
    glfwGetCursorPos(win, &x, &y);
    float px, py;
    pointer_to_center(win, x, y, &px, &py);
    if (g_app.current.mirror)
        px = -px;
    nekomote_session_touch(g_app.current.session, px, py);
}

static void on_scroll(GLFWwindow *win, double xoff, double yoff)
{
    (void)win;
    (void)xoff;
    double x, y;
    glfwGetCursorPos(win, &x, &y);
    float px, py;
    pointer_to_center(win, x, y, &px, &py);
    float new_scale = g_app.scale * (float)pow(1.1, yoff);
    if (new_scale < 0.05f) new_scale = 0.05f;
    if (new_scale > 5.0f) new_scale = 5.0f;
    float ratio = new_scale / g_app.scale;
    g_app.origin_x = px + (g_app.origin_x - px) * ratio;
    g_app.origin_y = py + (g_app.origin_y - py) * ratio;
    g_app.scale = new_scale;
    nekomote_session_set_scale(g_app.current.session, g_app.scale);
    nekomote_session_set_origin(g_app.current.session, g_app.origin_x, g_app.origin_y);
}

static void on_key(GLFWwindow *win, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS)
        return;
    switch (key)
    {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(win, GLFW_TRUE);
        break;
    case GLFW_KEY_SPACE:
        g_app.paused = !g_app.paused;
        nekomote_session_set_playback_state(
            g_app.current.session,
            g_app.paused ? NEKOMOTE_PLAYBACK_PAUSED : NEKOMOTE_PLAYBACK_RUNNING);
        break;
    case GLFW_KEY_R:
        nekomote_session_reset(g_app.current.session);
        break;
    case GLFW_KEY_F:
        fit_character();
        break;
    case GLFW_KEY_TAB:
        switch_character((g_app.char_index + 1) % g_app.catalog.character_count);
        break;
    case GLFW_KEY_B:
    case GLFW_KEY_N:
        if (g_app.catalog.background_count > 0)
            switch_background((g_app.bg_index + 1) % g_app.catalog.background_count);
        break;
    case GLFW_KEY_C:
        g_app.need_bg_clear = 1;
        break;
    default:
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)
        {
            int idx = key - GLFW_KEY_1;
            switch_character(idx);
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s --assets <dir> [options]\n", prog);
    fprintf(stderr, "  %s <model.nkm.json> <session.nks.json> [options]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --width <N>     Window width (default 800)\n");
    fprintf(stderr, "  --height <N>    Window height (default 600)\n");
    fprintf(stderr, "  --character <name>  Initial character\n");
    fprintf(stderr, "Keys: Tab=next, 1-9=select, B/N=next bg, C=clear bg, Space=pause, R=reset, F=fit, Esc=quit\n");
}

int main(int argc, char **argv)
{
    const char *assets_dir = NULL;
    const char *single_model = NULL;
    const char *single_session = NULL;
    const char *char_name = NULL;
    int width = 1920;
    int height = 1080;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc)
            assets_dir = argv[++i];
        else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
            width = atoi(argv[++i]);
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            height = atoi(argv[++i]);
        else if (strcmp(argv[i], "--character") == 0 && i + 1 < argc)
            char_name = argv[++i];
        else if (argv[i][0] != '-' && !single_model)
            single_model = argv[i];
        else if (argv[i][0] != '-' && !single_session)
            single_session = argv[i];
    }

    if (!assets_dir && (!single_model || !single_session))
    {
        print_usage(argv[0]);
        return 2;
    }

    printf("nekomote_abi_host — C ABI wallpaper verification platform\n");
    printf("  version: %s\n\n", nekomote_version());

    /* Build catalog */
    if (assets_dir)
    {
        scan_catalog(&g_app.catalog, assets_dir);
        printf("  characters: %d\n", g_app.catalog.character_count);
        printf("  backgrounds: %d\n", g_app.catalog.background_count);
        if (g_app.catalog.character_count == 0)
        {
            fprintf(stderr, "Error: no characters found in %s/characters\n", assets_dir);
            return 1;
        }
    }
    else
    {
        g_app.catalog.character_count = 1;
        strncpy(g_app.catalog.characters[0].name, "model", MAX_NAME - 1);
        strncpy(g_app.catalog.characters[0].model_path, single_model, MAX_PATH - 1);
        strncpy(g_app.catalog.characters[0].session_path, single_session, MAX_PATH - 1);
        const char *slash = strrchr(single_model, '/');
        if (slash)
        {
            size_t len = (size_t)(slash - single_model);
            memcpy(g_app.catalog.characters[0].directory, single_model, len);
            g_app.catalog.characters[0].directory[len] = '\0';
        }
    }

    /* Select initial character */
    g_app.char_index = 0;
    if (char_name)
    {
        for (int i = 0; i < g_app.catalog.character_count; i++)
            if (strcmp(g_app.catalog.characters[i].name, char_name) == 0)
            {
                g_app.char_index = i;
                break;
            }
    }
    g_app.bg_index = -1;
    g_app.scale = 1.0f;
    g_app.width = width;
    g_app.height = height;

    /* Init GLFW */
    if (!glfwInit())
    {
        fprintf(stderr, "Error: glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window = glfwCreateWindow(width, height, "NekoMote ABI Host", NULL, NULL);
    if (!window)
    {
        fprintf(stderr, "Error: glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        fprintf(stderr, "Error: gladLoadGL failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    printf("  GL: %s\n", glGetString(GL_VERSION));

    /* Sync to actual framebuffer size (HiDPI) before fit */
    glfwGetFramebufferSize(window, &g_app.width, &g_app.height);

    /* Load initial character */
    load_holder_with_gl(&g_app.current, &g_app.catalog.characters[g_app.char_index]);
    if (!g_app.current.session)
    {
        fprintf(stderr, "Error: failed to load initial character\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    nekomote_session_set_output_size(g_app.current.session, g_app.width, g_app.height);
    g_app.origin_x = 0.0f;
    g_app.origin_y = 400.0f;
    g_app.scale = 1.0f;
    nekomote_session_set_origin(g_app.current.session, g_app.origin_x, g_app.origin_y);
    nekomote_session_set_scale(g_app.current.session, g_app.scale);
    fit_character();
    printf("  current: %s (mirror=%d, gaze=%d, touch=%d, textures=%zu, audio=%d/%zu)\n",
           g_app.catalog.characters[g_app.char_index].name,
           g_app.current.mirror,
           nekomote_program_gaze_available(g_app.current.program),
           nekomote_program_touch_available(g_app.current.program),
           nekomote_program_texture_count(g_app.current.program),
           g_app.current.has_audio ? g_app.current.audio.clip_count : 0,
           nekomote_program_audio_count(g_app.current.program));

    /* Set callbacks */
    glfwSetFramebufferSizeCallback(window, on_resize);
    glfwSetCursorPosCallback(window, on_mouse_move);
    glfwSetMouseButtonCallback(window, on_mouse_button);
    glfwSetScrollCallback(window, on_scroll);
    glfwSetKeyCallback(window, on_key);

    printf("\nRunning. Tab=next, 1-9=select, B/N=bg, C=clear, Space=pause, R=reset, F=fit, Esc=quit\n\n");

    /* Main loop */
    double last_time = glfwGetTime();
    long frame_count = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        double now = glfwGetTime();
        double dt = now - last_time;
        last_time = now;
        if (dt > 0.25) dt = 0.25;

        /* Handle pending character switch */
        if (g_app.fade_state == FADE_PENDING)
        {
            printf("Switching to character: %s\n", g_app.catalog.characters[g_app.char_index].name);
            /* Stop audio thread before shallow-copy: the worker's dev pointer
               points at g_app.current.audio, so memset below would zero its
               mutex while the thread still holds it — deadlock. */
            if (g_app.current.has_audio) audio_destroy(&g_app.current.audio);
            g_app.current.has_audio = 0;
            g_app.old = g_app.current;
            g_app.has_old = 1;
            memset(&g_app.current, 0, sizeof(g_app.current));
            load_holder_with_gl(&g_app.current, &g_app.catalog.characters[g_app.char_index]);
            nekomote_session_set_output_size(g_app.current.session, g_app.width, g_app.height);
            g_app.origin_x = 0.0f;
            g_app.origin_y = 400.0f;
            g_app.scale = 1.0f;
            nekomote_session_set_origin(g_app.current.session, g_app.origin_x, g_app.origin_y);
            nekomote_session_set_scale(g_app.current.session, g_app.scale);
            fit_character();

            /* Re-apply background to new session */
            if (g_app.bg.has_background)
            {
                g_app.bg.has_background = 0;
                apply_background(&g_app.bg, g_app.current.session, g_app.bg.current_path, 0);
            }

            g_app.fade_state = FADE_CROSSFADE;
            g_app.fade_alpha = 0.0f;
            g_app.fade_start = now;
            printf("  mirror=%d, textures=%zu\n", g_app.current.mirror,
                   nekomote_program_texture_count(g_app.current.program));
        }

        /* Update cross-fade */
        if (g_app.fade_state == FADE_CROSSFADE)
        {
            double elapsed = now - g_app.fade_start;
            g_app.fade_alpha = (float)(elapsed / FADE_CHAR_SECONDS);
            if (g_app.fade_alpha >= 1.0f)
            {
                g_app.fade_alpha = 1.0f;
                g_app.fade_state = FADE_NONE;
                if (g_app.has_old)
                {
                    destroy_holder(&g_app.old);
                    g_app.has_old = 0;
                }
            }
        }

        /* Handle background switch */
        if (g_app.need_bg_switch)
        {
            switch_background(g_app.target_bg_index);
            if (g_app.target_bg_index >= 0 && g_app.target_bg_index < g_app.catalog.background_count)
            {
                printf("Switching to background: %s\n", g_app.catalog.backgrounds[g_app.target_bg_index].name);
                apply_background(&g_app.bg, g_app.current.session,
                                 g_app.catalog.backgrounds[g_app.target_bg_index].path,
                                 FADE_BG_SECONDS);
                g_app.bg_index = g_app.target_bg_index;
            }
            g_app.need_bg_switch = 0;
        }

        /* Handle background clear */
        if (g_app.need_bg_clear)
        {
            clear_background(&g_app.bg, g_app.current.session);
            g_app.bg_index = -1;
            printf("Background cleared\n");
            g_app.need_bg_clear = 0;
        }

        /* Advance sessions */
        nekomote_session_advance(g_app.current.session, dt);
        if (g_app.fade_state == FADE_CROSSFADE && g_app.has_old)
            nekomote_session_advance(g_app.old.session, dt);

        /* Audio: pop commands and feed RMS back */
        if (g_app.current.has_audio)
        {
            nekomote_audio_command cmd;
            while (nekomote_session_pop_audio_command(g_app.current.session, &cmd))
                audio_submit(&g_app.current.audio, cmd);
            nekomote_session_set_audio_level(g_app.current.session,
                                            audio_rms(&g_app.current.audio));
        }

        /* Prepare frames */
        nekomote_session_prepare_frame(g_app.current.session);
        if (g_app.fade_state == FADE_CROSSFADE && g_app.has_old)
            nekomote_session_prepare_frame(g_app.old.session);

        /* Render */
        render_frame();
        glfwSwapBuffers(window);
        frame_count++;

        if (frame_count % 300 == 0)
        {
            nekomote_render_frame f = nekomote_session_frame(g_app.current.session);
            printf("  frame %ld  dt=%.2fms  verts=%zu  cmds=%zu  char=%s  bg=%s\n",
                   frame_count, dt * 1000.0, f.vertex_count, f.command_count,
                   g_app.catalog.characters[g_app.char_index].name,
                   g_app.bg.has_background ? g_app.catalog.backgrounds[g_app.bg_index].name : "none");
        }
    }

    printf("\nRendered %ld frames. Shutting down.\n", frame_count);

    /* Cleanup */
    if (g_app.has_old) destroy_holder(&g_app.old);
    destroy_holder(&g_app.current);
    if (g_app.bg.renderer) nekomote_renderer_destroy(g_app.bg.renderer);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
