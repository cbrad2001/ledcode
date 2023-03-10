/*
 * demo_ledtrig.c
 * - Demonstrate how to flash an LED using a custom trigger.
 *      Author: Brian Fraser
 */
#include <linux/module.h>
#include <linux/miscdevice.h>		// for misc-driver calls.
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/leds.h>	
//#error Are we building this?	//debugging

// info:

// "dot" is the basic unit of time; we’ll use 200 ms.
// "dash" is three dot-times long.
//  dot/dash is separated by one dot-time. During this time the LED is off.
// Two letters in a message are separated by three dot-times. During this time the LED is off.
// Each break between words is equal to seven dot-times total (no additional 3 dot-time inter-character delay).


#define MY_DEVICE_FILE "morse-code"

/******************************************************
 * LED
 ******************************************************/
#include <linux/leds.h>

DEFINE_LED_TRIGGER(ledtrig_demo);

#define LED_ON_TIME_ms 100
#define LED_OFF_TIME_ms 900

static void my_led_blink(void)
{
	led_trigger_event(ledtrig_demo, LED_FULL);
	msleep(LED_ON_TIME_ms);
	led_trigger_event(ledtrig_demo, LED_OFF);
	msleep(LED_OFF_TIME_ms);
}

static void led_register(void)
{
	// Setup the trigger's name:
	led_trigger_register_simple("demo", &ledtrig_demo);
}

static void led_unregister(void)
{
	// Cleanup
	led_trigger_unregister_simple(ledtrig_demo);
}


/******************************************************
 * Callbacks
 ******************************************************/
static ssize_t my_write(struct file* file, const char *buff, size_t count, loff_t* ppos)
{
	int i;
	printk(KERN_INFO "demo_ledtrig: Flashing %d times for string.\n", count);

	// Blink once per character (-1 to skip end null)
	for (i = 0; i < count-1; i++) {
		my_led_blink();
	}


	//TODO

	// Return # bytes actually written.
	return count;
}

static ssize_t my_read(struct file *file, char *buf, size_t count, loff_t *f_pos)
{
	// TODO:


	return 0;

}


/******************************************************
 * Misc support
 ******************************************************/
// Callbacks:  (structure defined in <kernel>/include/linux/fs.h)
struct file_operations my_fops = {
	.owner    =  THIS_MODULE,
	.write    =  my_write,
	.read 	  =  my_read,
};

// Character Device info for the Kernel:
static struct miscdevice my_miscdevice = {
		.minor    = MISC_DYNAMIC_MINOR,         // Let the system assign one.
		.name     = MY_DEVICE_FILE,             // /dev/.... file.
		.fops     = &my_fops                    // Callback functions.
};


/******************************************************
 * Driver initialization and exit:
 ******************************************************/
static int __init my_init(void)
{
	int ret;
	printk(KERN_INFO "----> demo_misc driver init(): file /dev/%s.\n", MY_DEVICE_FILE);

	// Register as a misc driver:
	ret = misc_register(&my_miscdevice);

	// LED:
	led_register();

	return ret;
}

static void __exit my_exit(void)
{
	printk(KERN_INFO "<---- demo_misc driver exit().\n");

	// Unregister misc driver
	misc_deregister(&my_miscdevice);

	// LED:
	led_unregister();
}

module_init(my_init);
module_exit(my_exit);

MODULE_AUTHOR("cba52 & jwa359");
MODULE_DESCRIPTION("A simple test driver");
MODULE_LICENSE("GPL");