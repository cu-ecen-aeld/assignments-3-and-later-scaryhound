/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer implementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    size_t current_offset = 0;
    uint8_t index = buffer->out_offs;
    uint8_t count = 0;

    if (buffer == NULL || entry_offset_byte_rtn == NULL) {
        return NULL;
    }

    // Loop through the buffer up to 10 times to find the correct byte offset
    while (count < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        // If we hit an empty entry, the offset is out of bounds
        if (buffer->entry[index].size == 0) {
            break;
        }

        // Check if the requested offset falls within the current string
        if (char_offset < current_offset + buffer->entry[index].size) {
            *entry_offset_byte_rtn = char_offset - current_offset;
            return &buffer->entry[index];
        }

        // Move to the next string in the history
        current_offset += buffer->entry[index].size;
        index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        count++;
    }

    return NULL;
}

const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char *replaced_buffer = NULL;

    if (buffer == NULL || add_entry == NULL) {
        return NULL;
    }

    // If the buffer is full, we are about to overwrite the oldest entry. Save its pointer so it can be freed!
    if (buffer->full) {
        replaced_buffer = buffer->entry[buffer->out_offs].buffptr;
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // Write the new entry
    buffer->entry[buffer->in_offs] = *add_entry;
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // Check if we just filled the buffer
    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    }

    return replaced_buffer;
}

void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
