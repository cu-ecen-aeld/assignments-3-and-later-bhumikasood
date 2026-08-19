/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h"

// Function prototypes
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
static int aesd_setup_cdev(struct aesd_dev *dev);
loff_t aesd_llseek(struct file *filp, loff_t off, int whence);
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int aesd_init_module(void);
void aesd_cleanup_module(void);

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Bhumika Sood"); 
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("Open");

    // Locate device structure
    struct aesd_dev *dev;
	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);

	// Point to device data
	filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("Release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
	struct aesd_buffer_entry *entry;
    size_t offset = 0;
    ssize_t retval = 0;
    
    // Lock with mutexx
    if (mutex_lock_interruptible(&dev->lock))
    {
        PDEBUG("Could not acquire mutex");
        return -ERESTARTSYS;
    }

    // Search buffer for entry at offset
    PDEBUG("Seaching for entry at offset %ld", *f_pos);
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &offset);
    if (entry == NULL)
    {
        PDEBUG("Entry at offset is null");
        goto exit;
    }

    // Set count to size of entry read
    if (count > (entry->size - offset))
    {
        count = entry->size - offset;
    }
    PDEBUG("Read %zu bytes with offset %lld", count, *f_pos);

    // Copy data to user space buffer
    if (copy_to_user(buf, entry->buffptr + offset, count) != 0)
    {
        retval = -EFAULT;
        goto exit;
    }

    // Update pointer with number of bytes read and set return value
    retval = count;
    *f_pos += count;

exit:
    // Release mutex
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    char *newline = NULL;
    char *writeBuffer = NULL;
    ssize_t retval = -ENOMEM;

    // Lock with mutexx
    if (mutex_lock_interruptible(&dev->lock))
    {
        PDEBUG("Could not acquire mutex");
        return -ERESTARTSYS;
    }

    // Allocate size of entry buffer with count of new data to be appended
    writeBuffer = krealloc(dev->writeEntry.buffptr, dev->writeEntry.size + count, GFP_KERNEL);
    if (writeBuffer == NULL)
    {
        goto exit;
    }
    dev->writeEntry.buffptr = writeBuffer;

    // Copy from user space buffer
    if (copy_from_user((char *)(dev->writeEntry.buffptr + dev->writeEntry.size), buf, count))
    {
        retval = -EFAULT;
        goto exit;   
    }
    PDEBUG("Write %zu bytes with offset %lld", count, *f_pos);

    // Update size of buffer with number of bytes copied and set return value
    retval = count;
    dev->writeEntry.size += count;

    // Check if new line character has been copied
    newline = memchr(dev->writeEntry.buffptr, '\n', dev->writeEntry.size);
    if (newline != NULL)
    {
        // Free the oldest entry's buffer before it gets overwritten
        if (dev->buffer.full)
        {
            kfree(dev->buffer.entry[dev->buffer.in_offs].buffptr);
        }

        // Add completed entry to circular buffer
        aesd_circular_buffer_add_entry(&dev->buffer, &dev->writeEntry);
        
        // Reset entry information
        dev->writeEntry.buffptr = NULL;
        dev->writeEntry.size = 0;
    }

exit:
    // Release mutex    
    mutex_unlock(&dev->lock);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =          THIS_MODULE,
    .read =           aesd_read,
    .write =          aesd_write,
    .open =           aesd_open,
    .release =        aesd_release,
    .llseek =         aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);

    if (result < 0) 
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    // Initialize device to zero
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    // Initialize mutex
    mutex_init(&aesd_device.lock);    

    // Register character device
    result = aesd_setup_cdev(&aesd_device);

    if (result) 
    {
        unregister_chrdev_region(dev, 1);
    }

    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    cdev_del(&aesd_device.cdev);

    // Free data of entry buffer
    if (aesd_device.writeEntry.buffptr != NULL) 
    {
        kfree(aesd_device.writeEntry.buffptr);
    }

    // Loop thorugh circular buffer and free memory
    struct aesd_buffer_entry *entry;
    uint8_t index = 0;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) 
    {
        if (entry->buffptr != NULL) 
        {
            kfree(entry->buffptr);
        }
    }
    unregister_chrdev_region(devno, 1);
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t newPosition;

    switch (whence)
    {
        // Seek set
        case 0:
            newPosition = off;
            break;
        // Seek current
        case 1:
            newPosition = filp->f_pos + off;
            break;
        // Seek end
        case 2:
        { 
            // Calculate total size of circular buffer
            struct aesd_buffer_entry *entry;
            uint8_t index;
            loff_t totalSize = 0;
            AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index)
            {
                totalSize += entry->size;
            }

            newPosition = totalSize + off;
            break;
        }
        default:
            return -EINVAL;
    }

    if (newPosition < 0)
    {
        return -EINVAL;
    }

    PDEBUG("Current file position %lld updated to new file position %lld", filp->f_pos, newPosition);
    filp->f_pos = newPosition;

    return filp->f_pos;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retval = 0;
    struct aesd_seekto seek;
    struct aesd_dev *dev = filp->private_data;

    switch (cmd)
    {
        case AESDCHAR_IOCSEEKTO:
        {
            // Lock with mutexx
            if (mutex_lock_interruptible(&dev->lock))
            {
                PDEBUG("Could not acquire mutex");
                return -ERESTARTSYS;
            }

            // Copy from user space buffer
            if (copy_from_user(&seek, (struct aesd_seekto __user*) arg, sizeof(seek)))
            {
                retval = -EFAULT;
                goto exit;   
            }
            
            // Check the number of entries in the buffer
            int commandCount = 0;
            if (dev->buffer.full)
            {
                commandCount = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            }
            else
            {
                commandCount = (dev->buffer.in_offs - dev->buffer.out_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
                                % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            }

            // Return error for write command out of range
            if (seek.write_cmd >= commandCount)
            {
                retval = -EINVAL;
                goto exit;
            }

            // Find starting offset
            loff_t newPosition = 0;
            for (int i = 0; i < seek.write_cmd; i++)
            {
                uint8_t index = (dev->buffer.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
                newPosition += dev->buffer.entry[index].size;
            }
            // Calculate new position
            newPosition += seek.write_cmd_offset;

            // Store result
            filp->f_pos = newPosition;
            retval = 0;
            break;
        }
        default:
            return -ENOTTY;
    }

exit:
    // Release mutex    
    mutex_unlock(&dev->lock);
    return retval;
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);