#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include "cbuffer.h"
#include "timer.h"

#define MAX_SIZE_BUFF 100u

// Globales
////////////////////

#define IS_EMPTY(a) a==0
#define IS_FULL(a) a==0x03
#define ODD 0x02
#define EVEN 0x01
#define NOT_IN(a) a ^ 0x03

static struct file_operations proc_fops_rnd = {
    .read = proc_read_rnd,
    .open = proc_open_rnd,
    .release = proc_close_rnd
};

//static struct workqueue_struct* my_wq;
static struct work_struct my_work;
static cbuffer_t *cbuff;

static struct timer_list timer;

static int time_period = HZ/2;
static int emergency_th = 80;

static char state = EMPTY;
static char block = false;
static char flushing = false;

DEFINE_SPINLOCK(mutex);
DEFINE_SEMAPHORE(mutex_list);
struct semaphore cola;
LIST_HEAD(oddlist);
LIST_HEAD(evenlist);

struct proc_dir_entry *proc_cfg_entry, *proc_rnd_entry;

void _unsafe_clear_list(struct list_head *list);
int safemove_n(struct list_head *src, int n, struct list_head *dst);

// Carga y descarga del módulo
/////////////////////////////////////////
int init_module(void){
    DBG("[modtimer] Creando entrada /proc/modconfig");
    sema_init(&cola, 0);

    proc_cfg_entry = create_proc_entry(PROC_CFG,0666,NULL);
    if(!proc_cfg_entry){
        DBG("[modtimer] ERROR al crear entrada /proc/modconfig");
        return -ENOMEM;
    }

    proc_cfg_entry->read_proc = proc_read_cfg;
    proc_cfg_entry->write_proc = proc_write_cfg;

    DBG("[modtimer] Creando entrada /proc/modtimer");
    proc_rnd_entry = create_proc_entry(PROC_RND,0444,NULL);
    if(!proc_rnd_entry){
        DBG("[modtimer] ERROR al crear entrada /proc/modtimer");
        remove_proc_entry(PROC_CFG, NULL);
        return -ENOMEM;
    }

    proc_rnd_entry->proc_fops = &proc_fops_rnd;

    cbuff = create_cbuffer_t(MAX_SIZE_BUFF);
    
    INIT_WORK(&my_work, work_flush_cbuffer);
    init_timer(&timer);

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

    return snprintf(buff, len,"time_period = %d\nemergency_th = %d\n", time_period, emergency_th);
}
int proc_write_cfg(struct file *file, const char __user *buff, unsigned long len, void *data){
    char *kbuff = vmalloc(len);
    int value;
    DBGV("[modcfg] read config %d buff", len);

    if(!kbuff)
        return -ENOSPC;

    copy_from_user(kbuff, buff, len);

    if(sscanf(kbuff, "time_period %d", &value)){
        time_period = value;
    }else if(sscanf(kbuff, "emergency_th %d", &value)){
        emergency_th = value;
    }else{
        vfree(kbuff);
        return -EINVAL;
    }
    
    vfree(kbuff);
    return len;
}

// Callbacks Consumo de números 
///////////////////////////////////////
ssize_t proc_read_rnd (struct file *file, char __user *buff, size_t len, loff_t *offset){
    int max_elem = len / 4, tot = 0;
    LIST_HEAD(list_aux);
    list_item_t *aux, *elem = NULL;
    char entry[5];

    if(*((int *)file->private_data) == ODD)
        safemove_n(&oddlist, max_elem, &list_aux); //Puede bloquear
    else
        safemove_n(&evenlist, max_elem, &list_aux); //Puede bloquear

    list_for_each_entry_safe(elem, aux, &list_aux, links){
        max_elem = snprintf(entry,5,"%hhu\n",elem->data);
        list_del(&elem->links);
        vfree(elem);
        
        copy_to_user(buff,entry,max_elem);

        buff += max_elem;
        tot += max_elem;
    }

    return tot;
}

int proc_open_rnd (struct inode *inod, struct file *file){
    char *type = vmalloc(1);
    // Inicio sección crítica
    down(&mutex_list);
    if(IS_FULL(state)) {
        up(&mutex_list);
        DBG("[modtimer] ERROR: no puede haber más de 2 lectores");
        vfree(type);
        return -EPERM;
    }

    *type = (IS_EMPTY(state)? ODD : NOT_IN(state));
        
    state |= *type;
    file->private_data = type;
    
    if(IS_FULL(state)){    
        timer.expires = jiffies + time_period;
        timer.data = 0;
        timer.function = timer_generate_rnd;
        add_timer(&timer);
    }

    up(&mutex_list);
    // Fin sección crítica


    try_module_get(THIS_MODULE);

    return 0;
}

int proc_close_rnd (struct inode *inod, struct file *file){

    down(&mutex_list);
    if(IS_FULL(state)){
        state ^= *(char *)file->private_data;
        up(&mutex_list);

        del_timer_sync(&timer);
        flush_scheduled_work();
        
    }else
        up(&mutex_list);

    // Clear all structures.
    cbuff->size = 0;
    _unsafe_clear_list(&mylist); 

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
    spin_lock(&mutex);
    insert_cbuffer_t(cbuff, rnd);
    if(((size_cbuffer_t(cbuff) * 100) / MAX_SIZE_BUFF > emergency_th) && !flushing){
        //Planificar flush
        flushing = true;
        schedule_work(&my_work);
    }
    spin_unlock(&mutex);
    //Fin sección critica

    timer.expires = jiffies + time_period;
    add_timer(&timer);

}
void work_flush_cbuffer(struct work_struct *work){	/* Buttom-half */
    unsigned long flags;
    int nitems;
    unsigned char items[MAX_SIZE_BUFF];
    LIST_HEAD(list_aux);
    DBGV("Work event");
    // Entra sección critica
    spin_lock_irqsave(&mutex, flags);
    nitems = remove_items_cbuffer_t(cbuff,(char *)items,MAX_SIZE_BUFF);
    spin_unlock_irqrestore(&mutex, flags);
    // Sal sección crítica

    for(nitems; --nitems > 0;){
        list_item_t *a = vmalloc(sizeof(list_item_t));
        a->data = items[nitems];
        list_add(&a->links, &list_aux);
    }

    //inicio sección crítica
    down(&mutex_list);
    list_splice_tail(&list_aux, &mylist);
    
    if(block){
        block = false;
        up(&cola);
    }
    up(&mutex_list);
    //fin sección crítica
    
    // Entra sección critica
    spin_lock_irqsave(&mutex, flags);
    flushing = false;
    spin_unlock_irqrestore(&mutex, flags);
    // Sal sección crítica
}


// Funciones auxiliares
/////////////////////////////////

void _unsafe_clear_list(struct list_head *list){
    list_item_t *aux, *elem = NULL;

    list_for_each_entry_safe(elem, aux, list, links){
        list_del(&(elem->links));
        vfree(elem);
    }

}

int safemove_n(struct list_head *origin, int n, struct list_head *dest){

    list_item_t *aux, *elem = NULL;
    int i = 0;

    DBGV("[Modtimer] Move n to list.");

    do{
        // Entra en la sección crítica
        if(down_interruptible(&mutex_list))
            return 0; 
        
        list_for_each_entry_safe(elem, aux, origin, links){
            if(++i > n){
                --i;
                break;
            }
            list_move(&(elem->links), dest);
        }

        if(!i){
            block = true;
            up(&mutex_list);   
            if(down_interruptible(&cola)){
                block = false; 
                return 0;
            }
        }

    }while(!i);

    up(&mutex_list); // Sale de la sección critica

    return i;
}
