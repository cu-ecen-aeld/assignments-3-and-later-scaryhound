/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes (Modified by Your Name)
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
#include <linux/slab.h> // kmalloc, kfree, krealloc
#include <linux/uaccess.h> // copy_to_user, copy_from_user
#include <linux/mutex.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Your Name"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");
    
    // Link the file pointer private data to our device struct
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    // Nothing to clean up here since memory is managed globally
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset_byte = 0;
    size_t bytes_to_read = 0;
    size_t uncopied_bytes = 0;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    // Lock the device to prevent concurrent reads/writes
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    // Find the correct string and offset in the circular buffer
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset_byte);
    
    if (entry == NULL) {
        // EOF reached
        mutex_unlock(&dev->lock);
        return 0; 
    }

    // Calculate how much we can read from this specific entry
    bytes_to_read = entry->size - entry_offset_byte;
    if (bytes_to_read > count) {
        bytes_to_read = count;
    }

    // Copy data from kernel space to user space
    uncopied_bytes = copy_to_user(buf, entry->buffptr + entry_offset_byte, bytes_to_read);
    if (uncopied_bytes) {
        retval = -EFAULT;
    } else {
        retval = bytes_to_read;
        *f_pos += bytes_to_read;
    }

    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    char *write_data = NULL;
    size_t uncopied_bytes = 0;
    const char *replaced_buffer = NULL;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    // Allocate memory for the incoming string
    write_data = kmalloc(count, GFP_KERNEL);
    if (!write_data) {
        return -ENOMEM;
    }

    // Copy the incoming string from user space into kernel memory
    uncopied_bytes = copy_from_user(write_data, buf, count);
    if (uncopied_bytes) {
        kfree(write_data);
        return -EFAULT;
    }

    // Lock the device to prevent race conditions while appending
    if (mutex_lock_interruptible(&dev->lock)) {
        kfree(write_data);
        return -ERESTARTSYS;
    }

    // Append the new data to the working entry
    if (dev->working_entry_size == 0) {
        dev->working_entry = write_data;
        dev->working_entry_size = count;
    } else {
        // Reallocate the working entry to fit the new appended bytes
        dev->working_entry = krealloc(dev->working_entry, dev->working_entry_size + count, GFP_KERNEL);
        if (!dev->working_entry) {
            kfree(write_data);
            mutex_unlock(&dev->lock);
            return -ENOMEM;
        }
        memcpy(dev->working_entry + dev->working_entry_size, write_data, count);
        dev->working_entry_size += count;
        kfree(write_data); // Free the temp buffer since it is copied
    }

    // If the write request ends with a newline, commit it to the circular buffer
    if (dev->working_entry[dev->working_entry_size - 1] == '\n') {
        struct aesd_buffer_entry new_entry;
        new_entry.buffptr = dev->working_entry;
        new_entry.size = dev->working_entry_size;

        // Add it to the buffer. If it overwrites an old entry, free the old memory!
        replaced_buffer = aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);
        if (replaced_buffer != NULL) {
            kfree(replaced_buffer);
        }

        // Reset the working entry for the next write command
        dev->working_entry = NULL;
        dev->working_entry_size = 0;
    }

    retval = count;
    mutex_unlock(&dev->lock);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t res;
    loff_t total_size = 0;
    int i;
    struct aesd_buffer_entry *entry;

    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, i) {
        if (entry->buffptr) {
            total_size += entry->size;
        }
    }

    res = fixed_size_llseek(filp, offset, whence, total_size);

    mutex_unlock(&dev->lock);
    return res;
}


long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    loff_t new_pos = 0;
    int i;
    struct aesd_buffer_entry *entry;
    long retval = 0;
    uint32_t num_cmds = 0;

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

    switch (cmd) {
        case AESDCHAR_IOCSEEKTO:
            if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto))) {
                return -EFAULT;
            }

            if (mutex_lock_interruptible(&dev->lock)) {
                return -ERESTARTSYS;
            }

            AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, i) {
                if (entry->buffptr) {
                    if (num_cmds == seekto.write_cmd) {
                        if (seekto.write_cmd_offset >= entry->size) {
                            retval = -EINVAL;
                            goto unlock;
                        }
                        
                        new_pos += seekto.write_cmd_offset;
                        filp->f_pos = new_pos;
                        goto unlock;
                    }
                    new_pos += entry->size;
                    num_cmds++;
                }
            }
            
            retval = -EINVAL;
            
        unlock:
            mutex_unlock(&dev->lock);
            break;
            
        default:
            retval = -ENOTTY;
            break;
    }

    return retval;
}


struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
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
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    // Initialize mutex, circular buffer, and working entry
    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.buffer);
    aesd_device.working_entry = NULL;
    aesd_device.working_entry_size = 0;

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    uint8_t index;
    struct aesd_buffer_entry *entry;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    // Free any incomplete write data that never got a newline
    if (aesd_device.working_entry != NULL) {
        kfree(aesd_device.working_entry);
    }

    // Free all the memory allocated for each string in the circular buffer
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        if (entry->buffptr != NULL) {
            kfree(entry->buffptr);
        }
    }
    
    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
