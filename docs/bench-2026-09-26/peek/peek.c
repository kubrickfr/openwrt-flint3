// peek: debugfs MMIO peek/poke for bench experiments. echo "r 0x3a01fc20" or "w 0x3a01fc20 0x88998100" > /sys/kernel/debug/peek; cat it.
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/uaccess.h>
static struct dentry *d; static char out[64];
static ssize_t peek_write(struct file *f, const char __user *ub, size_t n, loff_t *o)
{
	char b[64]; unsigned long addr, val = 0; char op; void __iomem *m;
	if (n >= sizeof(b)) return -EINVAL;
	if (copy_from_user(b, ub, n)) return -EFAULT;
	b[n] = 0;
	if (sscanf(b, "%c %li %li", &op, &addr, &val) < 2) return -EINVAL;
	m = ioremap(addr & ~3UL, 4); if (!m) return -ENOMEM;
	if (op == 'w') writel(val, m);
	snprintf(out, sizeof(out), "%08lx: %08x\n", addr, readl(m));
	iounmap(m); return n;
}
static ssize_t peek_read(struct file *f, char __user *ub, size_t n, loff_t *o)
{ return simple_read_from_buffer(ub, n, o, out, strlen(out)); }
static const struct file_operations fops = { .owner = THIS_MODULE, .write = peek_write, .read = peek_read };
static int __init peek_init(void) { d = debugfs_create_file("peek", 0600, NULL, NULL, &fops); return 0; }
static void __exit peek_exit(void) { debugfs_remove(d); }
module_init(peek_init); module_exit(peek_exit); MODULE_LICENSE("GPL");
