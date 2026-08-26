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
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Parthasrathi Bhowmick");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("aesd character device module implementing a ring buffer");

struct aesd_dev aesd_device;

void dump_ring_buffer(void);

void dump_ring_buffer() {
#if 0
    int i;
    struct aesd_buffer_entry entry;
    PDEBUG("Dumping ring buffer:\n");
    struct aesd_circular_buffer *ring_buffer = &aesd_device.ring_buffer;
    PDEBUG("Ring buffer size: %zu\n", ring_buffer.size_in_bytes);
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        entry = ring_buffer->entries[i];
        PDEBUG("entry[%d]: %p\n", i, entry.buffptr);
        PDEBUG("entry[%d].size: %zu\n", i, entry.size);
        if (entry.buffptr != NULL) {
            PDEBUG("entry[%d] value: |%.*s|\n", i, (int)entry.size, entry.buffptr);
        }
    }

    PDEBUG("Backing: %.*s\n", (int)aesd_device.entry_to_be_commited.size,  aesd_device.entry_to_be_commited.buffptr);
    PDEBUG("================================================");
#endif
}

int aesd_open(struct inode *inode, struct file *filp)
{
    filp->private_data = container_of(inode->i_cdev, struct aesd_dev, cdev);

    PDEBUG("file opened\n");
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("open file released\n");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval;
    struct aesd_dev *aesd_device = NULL;
    struct aesd_circular_buffer *buffer = NULL;
    struct aesd_buffer_entry *entry = NULL;
    size_t offset_in_entry;
    size_t read;

    PDEBUG("reading %zu bytes with offset %lld\n", count, *f_pos);

    retval = 0;
    aesd_device = (struct aesd_dev *) filp->private_data;
    buffer = &aesd_device->ring_buffer;

    mutex_lock(&aesd_device->mutex);
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(buffer,
            *f_pos, &offset_in_entry);
    if (entry == NULL) {
        mutex_unlock(&aesd_device->mutex);
        goto cleanup;
    }

    read = entry->size - offset_in_entry;
    if (copy_to_user(buf, entry->buffptr+offset_in_entry, read)) {
        mutex_unlock(&aesd_device->mutex);
        retval = -EFAULT;
        goto cleanup;
    }

    mutex_unlock(&aesd_device->mutex);

    *f_pos += read;
    retval = read;

cleanup:
    dump_ring_buffer();
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *ubuf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    size_t packet_size;
    int newline_found;
    char *kbuf = NULL;
    size_t i = 0;
    struct aesd_dev *aesd_device = NULL;
    struct aesd_circular_buffer *ring_buffer;
    Entry *pending_entry;

    PDEBUG("write %zu bytes with offset %lld\n", count, *f_pos);

    aesd_device = (struct aesd_dev *) filp->private_data;
    pending_entry = &aesd_device->entry_to_be_commited;
    ring_buffer = &aesd_device->ring_buffer;

    kbuf = kzalloc(count, GFP_KERNEL);
    if (kbuf == NULL) return -ENOBUFS;

    if (copy_from_user(kbuf, ubuf, count)) {
        retval = -EFAULT;
        goto cleanup;
    }

    // It probably is a good idea to implement this using memchr,
    // but that would require pointer arithmetic to find the size
    // of the packet. marking TODO for later
    newline_found = 0;
    for (i = 0; i < count; i++) {
        if (kbuf[i] == '\n') {
            newline_found = 1;
            break;
        }
    }
    packet_size = (newline_found) ? i+1 : count;

    mutex_lock(&aesd_device->mutex);
    if (pending_entry->buffptr != NULL) { // pending entry exists
        char *new_buf = krealloc(pending_entry->buffptr,
                                          pending_entry->size + packet_size, GFP_KERNEL);
        if (new_buf == NULL) {
            packet_size = -ENOBUFS;
            goto cleanup;
        }

        memcpy(new_buf+pending_entry->size, kbuf, packet_size);

        if (newline_found) {
            Entry e = {
                .buffptr = new_buf,
                .size = pending_entry->size + packet_size,
            };

            kfree(ring_buffer->entries[ring_buffer->in_offs].buffptr);
            aesd_circular_buffer_add_entry(ring_buffer, &e);

            pending_entry->buffptr = NULL;
            pending_entry->size = 0;
        } else {
            pending_entry->buffptr = new_buf;
            pending_entry->size += packet_size;
        }
    } else {
        if (newline_found) {
            Entry e = {
                .buffptr = kbuf,
                .size = packet_size,
            };

            kfree(ring_buffer->entries[ring_buffer->in_offs].buffptr);
            aesd_circular_buffer_add_entry(ring_buffer, &e);
        } else {
            pending_entry->buffptr = kbuf;
            pending_entry->size = packet_size;
        }
        kbuf = NULL;
    }
    mutex_unlock(&aesd_device->mutex);

    retval = packet_size;

    PDEBUG("written %zu bytes with offset %lld\n", retval, *f_pos);
cleanup:
    if (kbuf != NULL) kfree(kbuf);
    dump_ring_buffer();
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *aesd_device = filp->private_data;
    size_t new_offset = 0;

    switch(whence) {
    case SEEK_SET:
        new_offset = offset;
        break;
    case SEEK_CUR:
        new_offset = filp->f_pos + offset;
        break;
    case SEEK_END:
        new_offset = aesd_device->ring_buffer.size_in_bytes + offset;
        break;
    default:
        return -EINVAL;
    }

    if (new_offset < 0) {
        return -EINVAL;
    }

    filp->f_pos = new_offset;
    return new_offset;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_seekto seek_cmd;
    struct aesd_dev *aesd_device = filp->private_data;
    struct aesd_circular_buffer *buffer = &aesd_device->ring_buffer;
    size_t true_index;
    size_t prev_write_size;
    long f_pos;
    size_t i;

    static_assert(AESDCHAR_IOC_MAXNR == 1);
    switch (cmd) {
    case AESDCHAR_IOCSEEKTO:
        memset(&seek_cmd, 0, sizeof(seek_cmd));
        if (copy_from_user(&seek_cmd, (void __user *) arg, sizeof(seek_cmd)))
            return -EFAULT;

        PDEBUG("ioctl issued: write=%d off=%d\n", seek_cmd.write_cmd, seek_cmd.write_cmd_offset);

        if (seek_cmd.write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
            return -EINVAL;

        true_index = (buffer->out_offs + seek_cmd.write_cmd) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        PDEBUG("ioctl: true_index=%zu", true_index);
        if (seek_cmd.write_cmd_offset >= buffer->entries[true_index].size)
            return -EINVAL;

        prev_write_size = 0;
        for (i = buffer->out_offs;
                i < true_index;
                i = (i+1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
            prev_write_size += buffer->entries[i].size;
        }

        f_pos = prev_write_size + seek_cmd.write_cmd_offset;
        break;

    default:
        return -ENOIOCTLCMD;
    }

    filp->f_pos = f_pos;
    return 0;
}

struct file_operations aesd_fops = {
    .owner          = THIS_MODULE,
    .read           = aesd_read,
    .write          = aesd_write,
    .open           = aesd_open,
    .release        = aesd_release,
    .llseek         = aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
};

static int aesd_setup_cdev(struct cdev *cdev)
{
    int err;
    int devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(cdev, &aesd_fops);
    cdev->owner = THIS_MODULE;
    cdev->ops = &aesd_fops;
    err = cdev_add(cdev, devno, 1);
    if (err)
        printk(KERN_ERR "Error %d adding aesd cdev\n", err);
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
    if (result) {
        unregister_chrdev_region(dev, 1);
        return result;
    }

    PDEBUG("aesdchar driver loaded\n");
    return result;
}

void aesd_cleanup_module(void)
{
    int idx;
    struct aesd_buffer_entry *entry;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.ring_buffer, idx) {
        kfree(entry->buffptr);
    }

    kfree(aesd_device.entry_to_be_commited.buffptr);

    unregister_chrdev_region(devno, 1);
    if (mutex_is_locked(&aesd_device.mutex))
        mutex_unlock(&aesd_device.mutex);

    PDEBUG("aesdchar driver unloaded\n");
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
