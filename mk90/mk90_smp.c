#include "mk90_smp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mk90_smp_save_slot(mk90_smp_slot_t *slot)
{
    FILE *fp;

    if (!slot->present || !slot->dirty || slot->size >= 0100000u ||
        slot->path[0] == '\0') {
        return 0;
    }

    fp = fopen(slot->path, "wb");
    if (!fp) {
        return -1;
    }
    if (slot->size != 0 &&
        fwrite(slot->data, 1, slot->size, fp) != slot->size) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    slot->dirty = 0;
    return 0;
}

static void mk90_smp_release_slot(mk90_smp_slot_t *slot)
{
    if (!slot) {
        return;
    }
    (void)mk90_smp_save_slot(slot);
    free(slot->data);
    memset(slot, 0, sizeof(*slot));
}

void mk90_smp_reset(mk90_state_t *state)
{
    unsigned slot;

    for (slot = 0; slot < 2u; slot++) {
        state->smp[slot].position = 0;
        state->smp[slot].cmd = 0;
    }
}

void mk90_smp_close(mk90_state_t *state)
{
    unsigned slot;

    for (slot = 0; slot < 2u; slot++) {
        mk90_smp_release_slot(&state->smp[slot]);
    }
}

int mk90_smp_load(mk90_state_t *state, unsigned slot_index, const char *path,
                  char *err, size_t err_len)
{
    FILE *fp;
    long file_size;
    mk90_smp_slot_t *slot;

    if (slot_index >= 2u) {
        if (err && err_len) {
            snprintf(err, err_len, "Invalid SMP slot");
        }
        return -1;
    }

    slot = &state->smp[slot_index];
    mk90_smp_release_slot(slot);

    if (!path || path[0] == '\0') {
        return 0;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        if (err && err_len) {
            snprintf(err, err_len, "Failed to open %s", path);
        }
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        if (err && err_len) {
            snprintf(err, err_len, "Failed to seek %s", path);
        }
        return -1;
    }
    file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        if (err && err_len) {
            snprintf(err, err_len, "Failed to size %s", path);
        }
        return -1;
    }
    rewind(fp);

    slot->data = (byte *)malloc((size_t)file_size);
    if (!slot->data) {
        fclose(fp);
        if (err && err_len) {
            snprintf(err, err_len, "Out of memory for %s", path);
        }
        return -1;
    }
    if (file_size != 0 &&
        fread(slot->data, 1, (size_t)file_size, fp) != (size_t)file_size) {
        fclose(fp);
        mk90_smp_release_slot(slot);
        if (err && err_len) {
            snprintf(err, err_len, "Failed to read %s", path);
        }
        return -1;
    }
    fclose(fp);

    slot->size = (size_t)file_size;
    slot->mask = (slot->size < 0100000u) ? 0177777u : 077777777u;
    slot->present = 1;
    slot->dirty = 0;
    slot->position = 0;
    slot->cmd = 0;
    snprintf(slot->path, sizeof(slot->path), "%s", path);
    return 0;
}

byte mk90_smp_cmd(mk90_state_t *state, unsigned slot_index, byte value)
{
    if (slot_index < 2u) {
        state->smp[slot_index].cmd = value;
    }
    return 0;
}

byte mk90_smp_data(mk90_state_t *state, unsigned slot_index, byte value)
{
    mk90_smp_slot_t *slot;
    byte result = 0377u;

    if (slot_index >= 2u) {
        return result;
    }

    slot = &state->smp[slot_index];
    if (!slot->present || !slot->data) {
        return result;
    }

    switch (slot->cmd & 0360u) {
    case 0000u:
        result = 0;
        break;
    case 0240u:
        slot->position = ((slot->position << 8) | value) & slot->mask;
        break;
    case 0020u:
    case 0320u:
        if (slot->position < slot->size) {
            result = slot->data[slot->position];
        } else {
            result = 0;
        }
        if ((slot->cmd & 0200u) == 0) {
            slot->position = (slot->position - 1u) & slot->mask;
        } else {
            slot->position = (slot->position + 1u) & slot->mask;
        }
        break;
    case 0040u:
    case 0300u:
    case 0340u:
        if (slot->position < slot->size && slot->size < 0100000u) {
            slot->data[slot->position] = value;
            slot->dirty = 1;
        }
        if ((slot->cmd & 0020u) == 0) {
            slot->position = (slot->position + 1u) & slot->mask;
        } else {
            slot->position = (slot->position - 1u) & slot->mask;
        }
        break;
    default:
        break;
    }

    return result;
}
