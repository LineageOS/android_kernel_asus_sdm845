#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/platform_device.h>

//ASUS_BSP add read unique_id for factory++++
static struct proc_dir_entry *project_lcd_unique_id_proc_file;
static int project_lcd_unique_id_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%s\n", lcd_unique_id);
	return 0;
}

static int project_lcd_unique_id_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, project_lcd_unique_id_proc_read, NULL);
}


static struct file_operations project_lcd_unique_id_proc_ops = {
	.open = project_lcd_unique_id_proc_open,
	.read = seq_read,
	.release = single_release,
};

static void create_project_lcd_unique_id_proc_file(void)
{
    printk("create_project_id_proc_file\n");
    project_lcd_unique_id_proc_file = proc_create("lcd_unique_id", 0444,NULL, &project_lcd_unique_id_proc_ops);
    if(project_lcd_unique_id_proc_file){
        printk("create project_lcd_unique_id_proc_file sucessed!\n");
    }else{
		printk("create project_lcd_unique_id_proc_file failed!\n");
    }
}
//ASUS_BSP add read unique_id for factory----

static int __init proc_asusPRJ_init(void)
{
	create_project_lcd_unique_id_proc_file();
	return 0;
}
module_init(proc_asusPRJ_init);
