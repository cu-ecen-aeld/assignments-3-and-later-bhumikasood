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

// Function prototypes
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
static int aesd_setup_cdev(struct aesd_dev *dev);
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
    if (copy_to_user(buf, entry->buffptr, count) != 0)
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
    ssize_t retval = -ENOMEM;

    // Lock with mutexx
    if (mutex_lock_interruptible(&dev->lock))
    {
        PDEBUG("Could not acquire mutex");
        return -ERESTARTSYS;
    }

    // Allocate size of entry buffer with count of new data to be appended
    dev->writeEntry.buffptr = krealloc(dev->writeEntry.buffptr, dev->writeEntry.size + count, GFP_KERNEL);
    if (dev->writeEntry.buffptr == NULL)
    {
        goto exit;
    }

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
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
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

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);