
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>

#define PROC_CFG "modconfig"
#define PROC_RND "modtimer"

#define TIME_DEBUG
#define DEBUG_VERBOSE

#ifdef TIME_DEBUG
    #define DBG(format, arg...) do { \
        printk(KERN_DEBUG "%s: " format "\n" , __func__ , ## arg); \
    } while (0)

    #ifdef DEBUG_VERBOSE
        #define DBGV DBG
    #else
        #define DBGV(format, args...) /* */
    #endif
#else
    #define DBG(format, arg...) /* */
    #define DBGV(format, args...) /* */
#endif

MODULE_AUTHOR("R. Sampedro Ruiz");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Aproximación simplista al proceso de manejo" \
                    " de tramas por una interfaz de red");
                    
typedef struct {
       unsigned char data;
       struct list_head links;
}list_item_t;

/*
 * Prototipos de funciones
 */
// Callbacks Configuración del módulo
int proc_read_cfg(char *, char **, off_t, int, int *, void *);
int proc_write_cfg(struct file *, const char __user *, unsigned long, void *);
// Callbacks Consumo de números 
ssize_t proc_read_rnd (struct file *, char __user *, size_t, loff_t *);
int proc_open_rnd (struct inode *, struct file *);
int proc_close_rnd (struct inode *, struct file *);
// Trabajos diferidos
void timer_generate_rnd(unsigned long ); 	/* Top-half */
void work_flush_cbuffer(struct work_struct *);	/* Buttom-half */

