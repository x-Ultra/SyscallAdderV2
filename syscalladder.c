#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kern_levels.h>
#include <linux/kallsyms.h>
//rename & delete file
#include <linux/syscalls.h>
//kmalloc declaration
#include <linux/slab.h>
//header  for kmalloc flags
#include <linux/gfp.h>
//mutex
#include <linux/mutex.h>
//stuff for reading/wrinting on file (via VFS)
#include <linux/fs.h>
#include <asm/segment.h>
#include <linux/uaccess.h>
#include <linux/buffer_head.h>
//read_cr0
#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,3,0)
    #include <asm/switch_to.h>
#else
    #include <asm/system.h>
#endif

#ifndef X86_CR0_WP
#define X86_CR0_WP 0x00010000
#endif


#include "constants/constants.h"
#include "include/usctm.h"
#include "include/syscalladder_utils.h"

//Exporting functions to be usable
int syscall_adder(void* new_syscall, char *syscall_name_user, int sysname_len, int num_parameters);
int syscall_remover(int syscall_entrynumber);
EXPORT_SYMBOL(syscall_adder);
EXPORT_SYMBOL(syscall_remover);


asmlinkage int syscall_adder(void* new_syscall, char *syscall_name, int sysname_len, int num_parameters)
{
	int ret;
	//this should be the max length of the macro, but i'm lazy, 1PG should be ok.
	int DIM = 4096;
	int cst_entry;

	//TODO check for ononimous

	char *macro_line_0 = "#define %s() syscall(%d)\n";
	char *macro_line_1 = "#define %s(arg1) syscall(%d, arg1)\n";
	char *macro_line_2 = "#define %s(arg1, arg2) syscall(%d, arg1, arg2)\n";
	char *macro_line_3 = "#define %s(arg1, arg2, arg3) syscall(%d, arg1, arg2, arg3)\n";
	char *macro_line_4 = "#define %s(arg1, arg2, arg3, arg4) syscall(%d, arg1, arg2, arg3, arg4)\n";
	char *macro_line_5 = "#define %s(arg1, arg2, arg3, arg4, arg5) syscall(%d, arg1, arg2, arg3, arg4, arg5)\n";
	char *macro_line_6 = "#define %s(arg1, arg2, arg3, arg4, arg5, arg6) syscall(%d, arg1, arg2, arg3, arg4, arg5, arg6)\n";
	char *macro_line_used = NULL;

	if(try_module_get(THIS_MODULE) == 0){
		printk(KERN_ERR "%s: Module in use, try later\n", MODNAME);
		return -1;
	}

	mutex_lock_interruptible(&mod_mutex);
	//adding entry in syscall table
	cst_entry = update_syscalltable_entry(new_syscall, syscall_name);
	if(cst_entry == -1){
		printk(KERN_ERR "%s: Free entry not found while adding %s\n", MODNAME, syscall_name);
		module_put(THIS_MODULE);
		mutex_unlock(&mod_mutex);
		return -1;
	}
	mutex_unlock(&mod_mutex);

	if((macro_line_used = kmalloc(DIM, GFP_KERNEL)) == NULL){
		printk(KERN_ERR "%s: Unable to kmalloc\n", MODNAME);

		module_put(THIS_MODULE);
		return -1;
	}

	switch (num_parameters){
		case 0:
			ret = snprintf(macro_line_used, DIM, macro_line_0, syscall_name, cst_entry);
			break;
		case 1:
			ret = snprintf(macro_line_used, DIM, macro_line_1, syscall_name, cst_entry);
			break;
		case 2:
			ret = snprintf(macro_line_used, DIM, macro_line_2, syscall_name, cst_entry);
			break;
		case 3:
			ret = snprintf(macro_line_used, DIM, macro_line_3, syscall_name, cst_entry);
			break;
		case 4:
			ret = snprintf(macro_line_used, DIM, macro_line_4, syscall_name, cst_entry);
			break;
		case 5:
			ret = snprintf(macro_line_used, DIM, macro_line_5, syscall_name, cst_entry);
			break;
		case 6:
			ret = snprintf(macro_line_used, DIM, macro_line_6, syscall_name, cst_entry);
			break;
		default:
			printk(KERN_ERR "%s: Invalid number parameter (%d)\n", MODNAME, num_parameters);

			kfree(macro_line_used);
			module_put(THIS_MODULE);
			return -1;
	}

	if(ret <= 0){
		printk(KERN_ERR "%s: snprintf failed, returning (%d)\n", MODNAME, ret);

		kfree(macro_line_used);
		module_put(THIS_MODULE);
		return -1;
	}

	mutex_lock_interruptible(&mod_mutex);
	//inserting the line in the macro file
	if(insert_macro_line(cst_entry, macro_line_used) == -1){
		printk(KERN_ERR "%s: Unable to insert macro line\n", MODNAME);

		mutex_unlock(&mod_mutex);
		module_put(THIS_MODULE);
		kfree(macro_line_used);
		return -1;
	}
	mutex_unlock(&mod_mutex);


	kfree(macro_line_used);
	module_put(THIS_MODULE);
	return cst_entry;
}

asmlinkage int syscall_remover(int syscall_entrynumber)
{
	int i;

	if(!uninstalling){
		if(try_module_get(THIS_MODULE) == 0){
			printk(KERN_ERR "%s: Module in use, try later\n", MODNAME_R);
			return -1;
		}
	}

	mutex_lock_interruptible(&mod_mutex);

	for(i = 0; i < total_syscall_added; ++i){
		if(syscall_cts_numbers[i] == syscall_entrynumber){
			syscall_cts_numbers[i] = syscall_cts_numbers[total_syscall_added-1];
			syscall_cts_numbers[total_syscall_added-1] = 0;

			kfree(syscall_names[i]);
			syscall_names[i] = syscall_names[total_syscall_added-1];
			syscall_names[total_syscall_added-1] = 0;
			total_syscall_added--;
			break;
		}
	}

	//fixing syscall table
	cr0 = read_cr0();
	unprotect_memory();
	hacked_syscall_tbl[syscall_entrynumber] = (unsigned long *)hacked_ni_syscall;
	protect_memory();

	mutex_unlock(&mod_mutex);

	if(!uninstalling){
		module_put(THIS_MODULE);
	}

	printk(KERN_DEBUG "%s: Removed systemcall at indx %d \n", MODNAME_R, syscall_entrynumber);

	return 0;
}

static int __init install(void)
{
	//when a new syscall has to be added, the header created has to be included,
	//then call syscall(NR, void* syscall_toadd_definedinmodule), and the new
	//entry will be inserted.
	int i, j;

	//setting directory dir
	if(snprintf(CUSTOM_SYSCALL_MACROS, 512, CUSTOM_SYSCALL_MACROS_RAW, MACRO_DIR) <= 0){
		return -1;
	}

	//Using syscall_table_finder of Francesco Quaglia repository
	syscall_table_finder();

	if(!hacked_syscall_tbl){
		printk("%s: Failed to find the sys_call_table\n", MODNAME);
		return -1;
	}

	j=0;
	for(i=0;i<ENTRIES_TO_EXPLORE;i++){
		if(hacked_syscall_tbl[i] == hacked_ni_syscall){
			printk("%s: found sys_ni_syscall entry at syscall_table[%d]\n",MODNAME,i);
			free_entries[j++] = i;
			if(j>=MAX_FREE) break;
		}
	}
    j=0;
    for(i=0;i<ENTRIES_TO_EXPLORE;i++){
		if(hacked_syscall_tbl[i] == hacked_ni_syscall){
			printk("%s: found sys_ni_syscall entry at syscall_table[%d]\n",MODNAME,i);
			free_entries[j++] = i;
			if(j>=MAX_FREE) break;
		}
	}

	return 0;
}

static void __exit uninstall(void)
{
	printk(KERN_NOTICE "%s: WARNING, this module will not delete your installed syscalls, for dependencies reason", MODNAME);
}

module_init(install)
module_exit(uninstall)
MODULE_LICENSE("GPL");
