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
#include <linux/delay.h>
#include <linux/kfifo.h>
#include <linux/leds.h>
//#error Are we building this?	//debugging


#include "include/morseEncodings.h"
// info:

// "dot" is the basic unit of time; we’ll use 200 ms.
// "dash" is three dot-times long.

#define MY_DEVICE_FILE "morsecode"
#define FIFO_SIZE 256

#define DOT_UNIT 200 	//time in ms
#define DASH_UNIT (3*DOT_UNIT)

static DECLARE_KFIFO(queue,char,FIFO_SIZE);

/******************************************************
 * LED
 ******************************************************/
DEFINE_LED_TRIGGER(morse_led_trigger);

#define LED_ON_TIME_ms 100
#define LED_OFF_TIME_ms 900

static void my_led_on(void)
{
	led_trigger_event(morse_led_trigger, LED_FULL);
	msleep(DOT_UNIT);
}

static void my_led_off(void)
{
	led_trigger_event(morse_led_trigger, LED_OFF);
	msleep(DOT_UNIT);
}

static void led_register(void)		// Setup the trigger's name:
{
	led_trigger_register_simple("morsecode", &morse_led_trigger);
}

static void led_unregister(void)	// Cleanup
{
	led_trigger_unregister_simple(morse_led_trigger);
	led_trigger_event(morse_led_trigger, LED_OFF);
}


/******************************************************
 * Callbacks
 ******************************************************/
static short decipher_led_code(char ch)
{
	// Skip (with no delay) any character which is not a letter (a-z, or A-Z) or whitespace. 
	// (i.e., 'Hi There' should flash the same as '!H_I th_ER&e@#09.,-5%!' • 

	if (ch >= 'a' && ch <= 'z')
		return morsecode_codes[ch-'a'];

	if(ch>='A' && ch<='Z')
		return morsecode_codes[ch-'A'];
	
	// Trim whitespace from the front and end of the input. Whitespace is ‘ ’, ‘\n’, ‘\r’, ‘\t’. 
	// Identify the breaks between words by 1 or more whitespace between characters.
	if (ch == ' ')
	{
		return 0;
	}

	return 0;	//all else
}


static ssize_t my_write(struct file* file, const char *buff, size_t count, loff_t* ppos)
{
	int i = 0, deciphered = 0;
	printk(KERN_INFO "demo_ledtrig: Flashing %d times for string.\n", count);

	//blocking call, returns only after flashing done.
	// flash message to LED (loop characters in input, decipher flash code)
	
	//  dot/dash is separated by one dot-time. During this time the LED is off.
	// Two letters in a message are separated by three dot-times. During this time the LED is off.
	// Each break between words is equal to seven dot-times total (no additional 3 dot-time inter-character delay).
	for (i = 0; i < count; i++) {
		char ch;
		if (copy_from_user(&ch, &buff[i],sizeof(char))>0){	//loop thru all characters in input
			printk(KERN_INFO "ERROR: Unable to read byte %d",i);
			return -EFAULT;
		}
		deciphered = decipher_led_code(ch);					//determine flash code for each letter & skip spaces

		// Flash LEDs
		printk(KERN_INFO "Next character (%d) = %c\n",i,ch);
		
		if (deciphered == 1)							// on for letter
		{
			my_led_on();
		}
		else												// off for whitespace/non-letter
		{
			my_led_off();
		}

		//2 letters seperated by 3 dot times::
		msleep(DOT_UNIT*3);
	}


	//TODO

	// Return # bytes actually written.
	*ppos += count;
	return count;
}

static ssize_t my_read(struct file *file, char *buf, size_t count, loff_t *f_pos)
{
	//fill user buffer with data...
	int bytes_read = 0;
	if (kfifo_to_user(&queue, buf, count, &bytes_read)){
		return -EFAULT;
	}

	printk(KERN_INFO "morse::my_read(), buff size %d, f_pos %d\n", (int)count, (int)*f_pos);

	//return bytes actually read
	return bytes_read;

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
	printk(KERN_INFO "----> morse-code driver init(): file /dev/%s.\n", MY_DEVICE_FILE);

	// Register as a misc driver:
	ret = misc_register(&my_miscdevice);

	// QUEUE
	INIT_KFIFO(queue);

	// LED:
	led_register();

	return ret;
}

static void __exit my_exit(void)
{
	printk(KERN_INFO "<---- morse-code driver exit().\n");

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