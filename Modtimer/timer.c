#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include "cbuffer.h"
#include "timer.h"

#define MAX_SIZE_BUFF 100

// Globales
////////////////////

static struct file_operations proc_fops_rnd = {
    .read = proc_read_rnd,
    .open = proc_open_rnd,
    .release = proc_close_rnd
};

static struct workqueue_struct* my_wq;
static struct work_struct my_work;
static cbuffer_t *cbuff;

static struct timer_list timer;

static int time_period = HZ/2;
static int emergency_th = 80;

static char used = false;

DEFINE_SPINLOCK(mutex);
LIST_HEAD(mylist);

struct proc_dir_entry *proc_cfg_entry, *proc_rnd_entry;


// Carga y descarga del módulo
/////////////////////////////////////////
int init_module(void){
    DBG("[modtimer] Creando entrada /proc/modconfig");
    proc_cfg_entry = create_proc_entry(PROC_CFG,0666,NULL);
    if(!proc_cfg_entry){
        DBG("[modtimer] ERROR al crear entrada /proc/modconfig");
        return -ENOMEM;
    }

    proc_cfg_entry->read_proc = proc_read_cfg;
    proc_cfg_entry->write_proc = proc_write_cfg;

    DBG("[modtimer] Creando entrada /proc/modtimer");
    proc_rnd_entry = create_proc_entry(PROC_RND,0666,NULL);
    if(!proc_rnd_entry){
        DBG("[modtimer] ERROR al crear entrada /proc/modtimer");
        remove_proc_entry(PROC_CFG, NULL);
        return -ENOMEM;
    }

    proc_rnd_entry->proc_fops = &proc_fops_rnd;

    cbuff = create_cbuffer_t(MAX_SIZE_BUFF);

    return 0;
}
void cleanup_module(void){
    remove_proc_entry(PROC_CFG, NULL);
    remove_proc_entry(PROC_RND, NULL);

    destroy_cbuffer_t(cbuff);
}


// Callbacks Configuración del módulo
///////////////////////////////////////
int proc_read_cfg(char *buff, char **buff_loc, off_t offset, int len, int *eof, void *data){

    return snprintf(buff, len,"time_period = %d\nemergency_th = %d", time_period, emergency_th);
}
int proc_write_cfg(struct file *file, const char __user *buff, unsigned long len, void *data){

    return 0;
}

// Callbacks Consumo de números 
///////////////////////////////////////
ssize_t proc_read_rnd (struct file *file, char __user *buff, size_t len, loff_t *offset){

    return 0;
}

int proc_open_rnd (struct inode *inod, struct file *file){

    // Inicio sección crítica
    spin_lock(&mutex);
    if(used) {
        return -EPERM;
    }

    used = true;
    spin_unlock(&mutex);
    // Fin sección crítica

    init_timer(&timer);
    timer.expires = jiffies + time_period;
    timer.data = 0;
    timer.function = timer_generate_rnd;
    add_timer(&timer);

    try_module_get(THIS_MODULE);

    return 0;
}

int proc_close_rnd (struct inode *inod, struct file *file){

    del_timer_sync(&timer);
    // Clear all structures.

    used = false;
    module_put(THIS_MODULE);

    return 0;
}

// Trabajos diferidos
///////////////////////////////////////
void timer_generate_rnd(unsigned long data){ 		/* Top-half */
    char rnd = (char) jiffies;
    DBGV("Time event %hhX", rnd);

    // Inicio Sección crítica
    insert_cbuffer_t(cbuff, rnd);
    if((size_cbuffer_t(cbuff) * 100) / MAX_SIZE_BUFF > emergency_th){
        //Planificar flush
    }
    //Fin sección critica

    timer.expires = juffies + time_period;
    add_timer(&timer);

}
void work_flush_cbuffer(struct work_struct *work){	/* Buttom-half */
    DBGV("Work event");
}




