#include "shm.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"

typedef struct {
    int active;
    uint32_t id;
    uint64_t size;
    uint32_t num_frames;
    uint64_t frames[SHM_MAX_FRAMES];
} shm_segment_t;

static shm_segment_t segments[SHM_MAX_SEGMENTS];
static uint32_t next_shm_id = 1;

static shm_segment_t *find_segment(uint32_t id) {
    for (int i = 0; i < SHM_MAX_SEGMENTS; i++) {
        if (segments[i].active && segments[i].id == id) return &segments[i];
    }
    return 0;
}

uint32_t shm_create(uint64_t size) {
    uint32_t num_frames = (uint32_t)((size + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE);
    if (num_frames == 0 || num_frames > SHM_MAX_FRAMES) {
        serial_write("shm: HATA - gecersiz boyut\n");
        return 0;
    }

    int slot = -1;
    for (int i = 0; i < SHM_MAX_SEGMENTS; i++) {
        if (!segments[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        serial_write("shm: HATA - segment tablosu dolu\n");
        return 0;
    }

    for (uint32_t i = 0; i < num_frames; i++) {
        uint64_t f = pmm_alloc_frame();
        if (f == 0) {
            for (uint32_t j = 0; j < i; j++) pmm_free_frame(segments[slot].frames[j]);
            serial_write("shm: HATA - PMM'de bellek kalmadi\n");
            return 0;
        }
        segments[slot].frames[i] = f;
    }

    segments[slot].active = 1;
    segments[slot].id = next_shm_id++;
    segments[slot].size = size;
    segments[slot].num_frames = num_frames;

    serial_write("shm: olusturuldu id=");
    serial_write_hex(segments[slot].id);
    serial_write(" cerceve_sayisi=");
    serial_write_hex(num_frames);
    serial_write("\n");

    return segments[slot].id;
}

void *shm_attach(uint32_t id, uint64_t desired_virt_addr) {
    shm_segment_t *s = find_segment(id);
    if (!s) return 0;

    uint64_t va = desired_virt_addr & ~0xFFFULL;
    for (uint32_t f = 0; f < s->num_frames; f++) {
        if (vmm_map_page(va + (uint64_t)f * PMM_FRAME_SIZE, s->frames[f],
                          VMM_PRESENT | VMM_WRITABLE) != 0) {
            serial_write("shm: attach basarisiz (vmm_map_page)\n");
            return 0;
        }
    }

    serial_write("shm: attach edildi id=");
    serial_write_hex(id);
    serial_write(" adres=");
    serial_write_hex((uint32_t)(va >> 32));
    serial_write_hex((uint32_t)va);
    serial_write("\n");

    return (void *)(uintptr_t)va;
}

int shm_detach(uint32_t id, void *ptr) {
    shm_segment_t *s = find_segment(id);
    if (!s) return -1;

    uint64_t va = (uint64_t)(uintptr_t)ptr;
    for (uint32_t f = 0; f < s->num_frames; f++) {
        vmm_unmap_page(va + (uint64_t)f * PMM_FRAME_SIZE);
    }
    return 0;
}

int shm_destroy(uint32_t id) {
    shm_segment_t *s = find_segment(id);
    if (!s) return -1;

    for (uint32_t f = 0; f < s->num_frames; f++) pmm_free_frame(s->frames[f]);
    s->active = 0;

    serial_write("shm: yok edildi id=");
    serial_write_hex(id);
    serial_write("\n");
    return 0;
}
