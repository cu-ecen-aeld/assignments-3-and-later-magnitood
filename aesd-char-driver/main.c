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

#include <asm-generic/errno-base.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "aesd-circular-buffer.h"
#include "aesdchar.h"
#include "linux/gfp_types.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Parthasrathi Bhowmick");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("aesd character device module implementing a ring buffer");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("driver opened\n");
    struct aesd_dev *aesd_devp;

    struct aesd_instance_data *data = kzalloc(sizeof(*data), GFP_KERNEL);
    if (data == NULL) return -ENOMEM;

    data->aesd_dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = data;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("driver released\n");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    PDEBUG("read %zu bytes with offset %lld\n", count, *f_pos);

    ssize_t retval = 0;

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *ubuf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    unsigned char *kbuf = NULL;
    size_t i = 0;
    struct aesd_instance_data *instance_data = NULL;
    struct aesd_circular_buffer *ring_buffer;
    Entry *pending_entry;

    PDEBUG("write %zu bytes with offset %lld\n", count, *f_pos);

    instance_data = (struct aesd_instance_data *) filp->private_data;
    pending_entry = &instance_data->entry_to_be_commited;
    ring_buffer = &instance_data->aesd_dev->ring_buffer;

    kbuf = kzalloc(count, GFP_KERNEL);
    if (kbuf == NULL) return -ENOBUFS;

    int bytes_not_copied = copy_from_user(kbuf, ubuf, count);
    if (bytes_not_copied > 0) {
        retval = -EFAULT;
        goto cleanup;
    }

    int newline_found = 0;
    for (i = 0; i < count; i++) {
        if (kbuf[i] == '\n') {
            newline_found = 1;
            break;
        }
    }
    retval = i+1;

    if (pending_entry->buffptr != NULL) { // pending entry exists
        unsigned char *new_buf = krealloc(pending_entry->buffptr, pending_entry->size + i+1, GFP_KERNEL);
        if (new_buf == NULL) {
            retval = -ENOBUFS;
            goto cleanup;
        }

        memcpy(new_buf+pending_entry->size, kbuf, i+1);

        if (newline_found) {
            Entry e = {
                .buffptr = new_buf,
                .size = pending_entry->size + i+1,
            };

            mutex_lock(&instance_data->aesd_dev->mutex);
            kfree(ring_buffer->entries[ring_buffer->in_offs].buffptr);
            aesd_circular_buffer_add_entry(ring_buffer, &e);
            mutex_unlock(&instance_data->aesd_dev->mutex);

            pending_entry->buffptr = NULL;
            pending_entry->size = 0;
        } else {
            pending_entry->buffptr = new_buf;
            pending_entry->size += i+1;
        }
    } else {
        if (newline_found) {
            Entry e = {
                .buffptr = kbuf,
                .size = i + 1,
            };

            mutex_lock(&instance_data->aesd_dev->mutex);
            kfree(ring_buffer->entries[ring_buffer->in_offs].buffptr);
            aesd_circular_buffer_add_entry(ring_buffer, &e);
            mutex_unlock(&instance_data->aesd_dev->mutex);
        } else {
            pending_entry->buffptr = kbuf;
            pending_entry->size = i+1;
        }
        kbuf = NULL;
    }

cleanup:
    if (kbuf != NULL) kfree(kbuf);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct cdev *cdev)
{
    int err;
    int devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(cdev, &aesd_fops);
    cdev->owner = THIS_MODULE;
    cdev->ops = &aesd_fops;
    err = cdev_add(cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev\n", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    memset(&aesd_device, 0, sizeof(aesd_device));
    mutex_init(&aesd_device.mutex);

    result = aesd_setup_cdev(&aesd_device.cdev);
    if (result) { // result != 0
        unregister_chrdev_region(dev, 1);
        return result;
    }

    PDEBUG("aesdchar module loaded\n");
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    // TODO: cleanup AESD specific poritions here as necessary
    unregister_chrdev_region(devno, 1);
    if (mutex_is_locked(&aesd_device.mutex)) mutex_unlock(&aesd_device.mutex);

    PDEBUG("aesdchar module unloaded\n");
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
