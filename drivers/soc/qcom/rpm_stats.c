/* Copyright (c) 2011-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <asm/arch_timer.h>
#include <linux/time.h>
#include <linux/proc_fs.h>

#define RPM_STATS_NUM_REC	2
#define MSM_ARCH_TIMER_FREQ	19200000

#define GET_PDATA_OF_ATTR(attr) \
	(container_of(attr, struct msm_rpmstats_kobj_attr, ka)->pd)

extern u32 asus_debug_suspend;
extern bool asus_enter_suspend;

struct msm_rpmstats_record {
	char name[32];
	u32 id;
	u32 val;
};

struct msm_rpmstats_platform_data {
	phys_addr_t phys_addr_base;
	u32 phys_size;
	u32 num_records;
};

struct msm_rpmstats_private_data {
	void __iomem *reg_base;
	u32 num_records;
	u32 read_idx;
	u32 len;
	char buf[480];
	struct msm_rpmstats_platform_data *platform_data;
};

struct msm_rpm_stats_data {
	u32 stat_type;
	u32 count;
	u64 last_entered_at;
	u64 last_exited_at;
	u64 accumulated;
#if defined(CONFIG_MSM_RPM_SMD)
	u32 client_votes;
	u32 reserved[3];
#endif

};

struct msm_rpmstats_kobj_attr {
	struct kobject *kobj;
	struct kobj_attribute ka;
	struct msm_rpmstats_platform_data *pd;
};

static inline u64 get_time_in_sec(u64 counter)
{
	do_div(counter, MSM_ARCH_TIMER_FREQ);

	return counter;
}

static inline u64 get_time_in_msec(u64 counter)
{
	do_div(counter, MSM_ARCH_TIMER_FREQ);
	counter *= MSEC_PER_SEC;

	return counter;
}

static inline int msm_rpmstats_append_data_to_buf(char *buf,
		struct msm_rpm_stats_data *data, int buflength)
{
	char stat_type[5];
	u64 time_in_last_mode;
	u64 time_since_last_mode;
	u64 actual_last_sleep;

	stat_type[4] = 0;
	memcpy(stat_type, &data->stat_type, sizeof(u32));

	time_in_last_mode = data->last_exited_at - data->last_entered_at;
	time_in_last_mode = get_time_in_msec(time_in_last_mode);
	time_since_last_mode = arch_counter_get_cntvct() - data->last_exited_at;
	time_since_last_mode = get_time_in_sec(time_since_last_mode);
	actual_last_sleep = get_time_in_msec(data->accumulated);

#if defined(CONFIG_MSM_RPM_SMD)
	return snprintf(buf, buflength,
		"RPM Mode:%s\n\t count:%d\ntime in last mode(msec):%llu\n"
		"time since last mode(sec):%llu\nactual last sleep(msec):%llu\n"
		"client votes: %#010x\n\n",
		stat_type, data->count, time_in_last_mode,
		time_since_last_mode, actual_last_sleep,
		data->client_votes);
#else
	return snprintf(buf, buflength,
		"RPM Mode:%s\n\t count:%d\ntime in last mode(msec):%llu\n"
		"time since last mode(sec):%llu\nactual last sleep(msec):%llu\n\n",
		stat_type, data->count, time_in_last_mode,
		time_since_last_mode, actual_last_sleep);
#endif
}

static inline u32 msm_rpmstats_read_long_register(void __iomem *regbase,
		int index, int offset)
{
	return readl_relaxed(regbase + offset +
			index * sizeof(struct msm_rpm_stats_data));
}

static inline u64 msm_rpmstats_read_quad_register(void __iomem *regbase,
		int index, int offset)
{
	u64 dst;

	memcpy_fromio(&dst,
		regbase + offset + index * sizeof(struct msm_rpm_stats_data),
		8);
	return dst;
}

static inline int msm_rpmstats_copy_stats(
			struct msm_rpmstats_private_data *prvdata)
{
	void __iomem *reg;
	struct msm_rpm_stats_data data;
	int i, length;

	reg = prvdata->reg_base;

	for (i = 0, length = 0; i < prvdata->num_records; i++) {
		data.stat_type = msm_rpmstats_read_long_register(reg, i,
				offsetof(struct msm_rpm_stats_data,
					stat_type));
		data.count = msm_rpmstats_read_long_register(reg, i,
				offsetof(struct msm_rpm_stats_data, count));
		data.last_entered_at = msm_rpmstats_read_quad_register(reg,
				i, offsetof(struct msm_rpm_stats_data,
					last_entered_at));
		data.last_exited_at = msm_rpmstats_read_quad_register(reg,
				i, offsetof(struct msm_rpm_stats_data,
					last_exited_at));
		data.accumulated = msm_rpmstats_read_quad_register(reg,
				i, offsetof(struct msm_rpm_stats_data,
					accumulated));
#if defined(CONFIG_MSM_RPM_SMD)
		data.client_votes = msm_rpmstats_read_long_register(reg,
				i, offsetof(struct msm_rpm_stats_data,
					client_votes));
#endif

		length += msm_rpmstats_append_data_to_buf(prvdata->buf + length,
				&data, sizeof(prvdata->buf) - length);
		prvdata->read_idx++;
	}

	return length;
}

static ssize_t rpmstats_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	struct msm_rpmstats_private_data prvdata;
	struct msm_rpmstats_platform_data *pdata = NULL;
	ssize_t length;

	pdata = GET_PDATA_OF_ATTR(attr);

	prvdata.reg_base = ioremap_nocache(pdata->phys_addr_base,
					pdata->phys_size);
	if (!prvdata.reg_base) {
		pr_err("ERROR could not ioremap start=%pa, len=%u\n",
				&pdata->phys_addr_base, pdata->phys_size);
		return -EBUSY;
	}

	prvdata.read_idx = prvdata.len = 0;
	prvdata.platform_data = pdata;
	prvdata.num_records = pdata->num_records;

	if (prvdata.read_idx < prvdata.num_records)
		prvdata.len = msm_rpmstats_copy_stats(&prvdata);

	length = scnprintf(buf, prvdata.len, "%s", prvdata.buf);
	iounmap(prvdata.reg_base);
	return length;
}

static int asus_softap;
struct msm_rpmstats_platform_data asus_pdata;
u32 asus_rpm_count[2] = {0xffffffff, 0xffffffff};
#define MAX_TIME    10*60
#define softap_PROC_FILE "driver/asus_softap"

void asus_show_rpm_sleep_count (void) {
	struct msm_rpmstats_private_data prvdata;
	void __iomem *reg;
	struct msm_rpm_stats_data data[asus_pdata.num_records];
	int i;
	static u32 local_cxsd = 0;
	static bool first_count = false;
	static struct timeval last_time;
	static struct timeval now_time;

	if(!asus_pdata.phys_addr_base){
		pr_err("phys_addr_base is NULL!\n");
		return ;
	}

	prvdata.reg_base = ioremap_nocache(asus_pdata.phys_addr_base,
					asus_pdata.phys_size);
	if (!prvdata.reg_base) {
		printk("ERROR could not ioremap start=%pa, len=%u\n",
				&asus_pdata.phys_addr_base, asus_pdata.phys_size);
		return ;
	}

	reg = prvdata.reg_base;
	for (i = 0; i < asus_pdata.num_records; i++) {
		data[i].count = msm_rpmstats_read_long_register(reg, i,
				offsetof(struct msm_rpm_stats_data, count));
		asus_rpm_count[i] = data[i].count;
	}

	if(!asus_enter_suspend || asus_softap){ //skip enable softap
		if(asus_softap)
			first_count = false;
		goto DONE;
	}

	if((data[1].count >= 0) && (data[1].count == local_cxsd)){
		if(!first_count){
			do_gettimeofday(&now_time);
			first_count = true;
		}

		do_gettimeofday(&last_time);

		if((last_time.tv_sec-now_time.tv_sec) >= MAX_TIME){
			asus_debug_suspend = 1;
			first_count = false;
		}else{
			asus_debug_suspend = 0;
		}
	}else{
		local_cxsd = data[1].count;
		asus_debug_suspend = 0;
		first_count = false;
	}

DONE:
	printk("RPM Mode:aosd count=%d;cxsd count=%d;asus_enter_suspend=%d;asus_debug_suspend=%d\n", data[0].count, data[1].count, asus_enter_suspend, asus_debug_suspend);
	if(asus_enter_suspend)
		asus_enter_suspend = false;
	return ;
}

static ssize_t asus_softap_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data)
{
    char messages[256];

    if (len > 256) {
        len = 256;
    }

    memset(messages, 0, sizeof(messages));
    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    sscanf(messages,"%d",&asus_softap);
	pr_info("asus_softap=%d\n", asus_softap);

    return len;
}
static int asus_softap_proc_read(struct seq_file *buf, void *data)
{
    seq_printf(buf, "asus_softap=%d\n", asus_softap);
    return 0;
}
static int asus_softap_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, asus_softap_proc_read, NULL);
}

static const struct file_operations asus_softap_fops = {
    .owner = THIS_MODULE,
    .open = asus_softap_proc_open,
    .read = seq_read,
    .write = asus_softap_proc_write,
};

static void create_asus_softap_proc_file(void)
{
    struct proc_dir_entry *asus_softap = proc_create(softap_PROC_FILE, 0444, NULL, &asus_softap_fops);

    if(!asus_softap)
        pr_err("creat asus_softap proc inode failed!\n");
}

static ssize_t rpmlog_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	void __iomem* aop_log;

	u32 logmsg[240];
	char msg[35]={0};
	int i=0,j=0;

/*
struct aop_log_entry {
  uint32        timestamp_lo;
  uint32        message[2];
  uint32        data;
} *aop_log_data = NULL;
uint32 *log_counts;
*/
	aop_log = ioremap_nocache(0xC370000,0X400);
	if(!aop_log){
		pr_err("%s: ERROR richard could not ioremap ",__func__);
	}
	else
	{
		memcpy_fromio(logmsg,aop_log,sizeof(logmsg));
		for (i = 0; i < 60; i++) {
			snprintf(msg,sizeof(msg),"T:%08X M:%c%c%c%c%c%c%c%c D:%08X\n",logmsg[i*4+0],
				*((char *)(&logmsg[i*4+1]) + 0),
				*((char *)(&logmsg[i*4+1]) + 1),
				*((char *)(&logmsg[i*4+1]) + 2),
				*((char *)(&logmsg[i*4+1]) + 3),
				*((char *)(&logmsg[i*4+1]) + 4),
				*((char *)(&logmsg[i*4+1]) + 5),
				*((char *)(&logmsg[i*4+1]) + 6),
				*((char *)(&logmsg[i*4+1]) + 7),
				logmsg[i*4+3]);
			printk("%s",msg);
			j+= snprintf(buf+j, sizeof(msg), "%s", msg);
		}
	}

	return j;
}

static int msm_rpmstats_create_sysfs(struct platform_device *pdev,
				struct msm_rpmstats_platform_data *pd)
{
	struct kobject *rpmstats_kobj = NULL;
	struct msm_rpmstats_kobj_attr *rpms_ka = NULL;
	struct msm_rpmstats_kobj_attr *rpms_log = NULL;
	int ret = 0;

	rpmstats_kobj = kobject_create_and_add("system_sleep", power_kobj);
	if (!rpmstats_kobj) {
		pr_err("Cannot create rpmstats kobject\n");
		ret = -ENOMEM;
		goto fail;
	}

	rpms_ka = kzalloc(sizeof(*rpms_ka), GFP_KERNEL);
	if (!rpms_ka) {
		kobject_put(rpmstats_kobj);
		ret = -ENOMEM;
		goto fail;
	}

	rpms_ka->kobj = rpmstats_kobj;

	sysfs_attr_init(&rpms_ka->ka.attr);
	rpms_ka->pd = pd;
	rpms_ka->ka.attr.mode = 0444;
	rpms_ka->ka.attr.name = "stats";
	rpms_ka->ka.show = rpmstats_show;
	rpms_ka->ka.store = NULL;

	ret = sysfs_create_file(rpmstats_kobj, &rpms_ka->ka.attr);
	platform_set_drvdata(pdev, rpms_ka);

	rpms_log = kzalloc(sizeof(*rpms_log), GFP_KERNEL);
	if (rpms_log) {
		sysfs_attr_init(&rpms_log->ka.attr);
		rpms_log->pd = pd;
		rpms_log->ka.attr.mode = 0444;
		rpms_log->ka.attr.name = "aop_log";
		rpms_log->ka.show = rpmlog_show;
		rpms_log->ka.store = NULL;

		ret = sysfs_create_file(rpmstats_kobj, &rpms_log->ka.attr);
	}

fail:
	return ret;
}

static int msm_rpmstats_probe(struct platform_device *pdev)
{
	struct msm_rpmstats_platform_data *pdata;
	struct resource *res = NULL, *offset = NULL;
	u32 offset_addr = 0;
	void __iomem *phys_ptr = NULL;
	char *key;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	key = "phys_addr_base";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (!res)
		return -EINVAL;

	key = "offset_addr";
	offset = platform_get_resource_byname(pdev, IORESOURCE_MEM, key);
	if (offset) {
		/* Remap the rpm-stats pointer */
		phys_ptr = ioremap_nocache(offset->start, SZ_4);
		if (!phys_ptr) {
			pr_err("Failed to ioremap offset address\n");
			return -ENODEV;
		}
		offset_addr = readl_relaxed(phys_ptr);
		iounmap(phys_ptr);
	}

	pdata->phys_addr_base  = res->start + offset_addr;
	pdata->phys_size = resource_size(res);

	key = "qcom,num-records";
	if (of_property_read_u32(pdev->dev.of_node, key, &pdata->num_records))
		pdata->num_records = RPM_STATS_NUM_REC;

	memset(&asus_pdata, 0, sizeof(asus_pdata));
	memcpy(&asus_pdata, pdata, sizeof(asus_pdata));

	msm_rpmstats_create_sysfs(pdev, pdata);

	create_asus_softap_proc_file();

	return 0;
}

static int msm_rpmstats_remove(struct platform_device *pdev)
{
	struct msm_rpmstats_kobj_attr *rpms_ka;

	if (!pdev)
		return -EINVAL;

	rpms_ka = (struct msm_rpmstats_kobj_attr *)
			platform_get_drvdata(pdev);

	sysfs_remove_file(rpms_ka->kobj, &rpms_ka->ka.attr);
	kobject_put(rpms_ka->kobj);
	platform_set_drvdata(pdev, NULL);

	return 0;
}


static const struct of_device_id rpm_stats_table[] = {
	{ .compatible = "qcom,rpm-stats" },
	{ },
};

int asus_rpm_stats_resume(struct device *dev)
{
	asus_show_rpm_sleep_count();

	return 0;
}

static const struct dev_pm_ops asus_rpm_stats_ops = {
	.resume = asus_rpm_stats_resume,
};

static struct platform_driver msm_rpmstats_driver = {
	.probe = msm_rpmstats_probe,
	.remove = msm_rpmstats_remove,
	.driver = {
		.name = "msm_rpm_stat",
		.owner = THIS_MODULE,
		.of_match_table = rpm_stats_table,
		.pm = &asus_rpm_stats_ops,
	},
};
builtin_platform_driver(msm_rpmstats_driver);
