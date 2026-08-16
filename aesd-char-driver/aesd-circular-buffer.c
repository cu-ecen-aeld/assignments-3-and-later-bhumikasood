/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
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

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    // Track index of buffer structure
    uint8_t bufferStructureIndex = buffer->out_offs;

    while (true)
    {
        // Read size of buffer entry at output pointer
        size_t currentSize = buffer->entry[bufferStructureIndex].size;
        // Check if size is greater than char offset
        if (currentSize <= char_offset)
        {
            // Decrement char offset by size of element
            char_offset -= currentSize;

            // Increment output pointer
            bufferStructureIndex = (bufferStructureIndex + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

            // Check if output pointer is valid
            if (bufferStructureIndex == buffer->in_offs)
            {
                break;
            }
        }
        else 
        {
            *entry_offset_byte_rtn = char_offset;
            return &(buffer->entry[bufferStructureIndex]);
        }
    }
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    // Check if buffer is full
    if (buffer->full)
    {
        // Advance the output pointer or rollover to 0
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // Add entry at input pointer
    buffer->entry[buffer->in_offs] = *add_entry;

    // Increment input pointer or rollover to 0
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // Set the full flag if the input and output pointer are at the same location
    if (buffer->in_offs == buffer->out_offs)
    {
        buffer->full = true;
    }
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
