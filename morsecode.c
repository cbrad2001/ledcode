/*
 * Code 
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
#include <linux/string.h>
#include <linux/ctype.h>
//#error Are we building this?	//debugging

#include "include/morseEncodings.h"

#define MY_DEVICE_FILE "morse-code"
#define FIFO_SIZE 256

// "dot" is the basic unit of time; we’ll use 200 ms.
// "dash" is three dot-times long.
#define DOT_UNIT 200 	//time in ms
#define DASH_UNIT (3*DOT_UNIT)

enum morse{
	DOT,
	DASH
};

#define KERNEL_BUFF_SIZE 500

#define IS_WHITESPACE -1
#define IS_NOTALPHA -2

static DECLARE_KFIFO(queue,char,FIFO_SIZE);

/******************************************************
 * LED
 ******************************************************/
DEFINE_LED_TRIGGER(morse_code);

#define LED_ON_TIME_ms 100
#define LED_OFF_TIME_ms 900

//turns LED off
static void my_led_off(void)
{
	led_trigger_event(morse_code, LED_OFF);
	msleep(DOT_UNIT);
}

// Turns the LED on and sleeps for the corresponding morse entry's time
static void my_led_on(enum morse status)
{
	led_trigger_event(morse_code, LED_FULL);
	if (status == DOT)
		msleep(DOT_UNIT);
	else
		msleep(DASH_UNIT);

	my_led_off();	//after blinking turn off
}

static void led_register(void)		// Setup the trigger's name:
{
	led_trigger_register_simple("morse-code", &morse_code);
}

static void led_unregister(void)	// Cleanup
{
	led_trigger_unregister_simple(morse_code);
	led_trigger_event(morse_code, LED_OFF);
}


/******************************************************
 * Helper Functions
 ******************************************************/
// returns the encoding corresponding to an input character, whitespace or anything else
static short decipher_led_code(char ch)
{
	char lowercaseID = 'a', uppercaseID = 'A';	// subtract to key into right code sequence
	// Skip (with no delay) any character which is not a letter (a-z, or A-Z) or whitespace. 
	// (i.e., 'Hi There' should flash the same as '!H_I th_ER&e@#09.,-5%!' • 
	if (ch >= 'a' && ch <= 'z')					
		return morsecode_codes[ch-lowercaseID];	//treats characters like integers to seach the right ASCII in the encodings

	if(ch>='A' && ch<='Z')
		return morsecode_codes[ch-uppercaseID];
	
	// Identify the breaks between words by 1 or more whitespace between characters.
	if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r')
	{
		return IS_WHITESPACE;
	}

	return IS_NOTALPHA;	//all else
}

// Taken from https://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
// Function to remove leading and trailing whitespace
// Note that this function modifies original sting by removing trailing whitespace.
static char *trimwhitespace(char *str)
{
   	char *end;

   	// Trim leading space
   	while(isspace((unsigned char)*str)) str++;

  	if(*str == 0)  // All spaces?
    	return str;

  	// Trim trailing space
  	end = str + strlen(str) - 1;
  	while(end > str && isspace((unsigned char)*end)) end--;

  	// Write new null terminator character
  	end[1] = '\0';

  	return str;
}

// Taken from https://stackoverflow.com/questions/17770202/remove-extra-whitespace-from-a-string-in-c
// Function to trim extra whitespace characters in between words.
void strip_extra_spaces(char* str) {
  	int i, x;
  	for(i=x=0; str[i]; ++i)
    	if(!isspace(str[i]) || (i > 0 && !isspace(str[i-1])))
      	str[x++] = str[i];
  	str[x] = '\0';
}

// Takes in an encoded hex encoding and converts it to ASCII morse code which will
// then get pushed into the KFIFO
static int convert_to_morse(short deciphered)
{
	if (deciphered == IS_WHITESPACE) {
		// put two spaces into the kfifo here
		// since deciphering alphas already append a space to the end anyways
		int i;
		for (i = 0; i < 2; i++) {
			if (!kfifo_put(&queue, ' ')) {
				return -EFAULT;
			}
		}

		// Each break between words is equal to seven dot-times total
		for (i = 0; i < 7; i++) {
			my_led_off();
		}
	}
	else if (deciphered != IS_NOTALPHA) {
		// check the MSB if it is a dash, where if it is not then it is a dot
		// after checking, left shift by 4 if it is a dash and 2 if it is a dot
		// repeat until the value to check is a zero
		while (deciphered != 0) {

			// CONVERTS INTO A DASH, Adds Dash to queue, calls corresponding blink
			if ((deciphered & DASH_MASK) == DASH_MASK) {
				my_led_on(DASH);
				// put a dash into kfifo here
				if (!kfifo_put(&queue, '-')) {
					return -EFAULT;
				}
				deciphered = deciphered << 4;
			}

			// CONVERTS TO A DOT, adds dot to queue, calls corresponding blink
			else if ((deciphered & DOT_MASK) == DOT_MASK){
				my_led_on(DOT);
				// put a dot into kfifo here
				if (!kfifo_put(&queue, '.')) {
					return -EFAULT;
				}
				deciphered = deciphered << 2;
			}
			else {
				printk(KERN_ERR "Failed to decipher bit sequence as either a dot or a dash.");
				return -EFAULT;
			}
		}

		// Two letters in a message are separated by three dot-times. During this time the LED is off.
		// my_led_off();
		my_led_off();
		my_led_off();

		// put a single space into the kfifo here (after letter)
		if (!kfifo_put(&queue, ' ')) {
			return -EFAULT;
		}
	}
	return 0;
}

/******************************************************
 * Callbacks
 ******************************************************/

// Called when user space application tries to write to the character device
// Eg: echo "hi" | sudo tee /dev/morse-code
static ssize_t my_write(struct file* file, const char *buff, size_t count, loff_t* ppos)
{
	int maxStringLength = count < KERNEL_BUFF_SIZE ? count : KERNEL_BUFF_SIZE;

	int i = 0, deciphered = 0;
	char kernelBuff[KERNEL_BUFF_SIZE];
	char *trimmedString = NULL;
	memset(kernelBuff, 0, KERNEL_BUFF_SIZE);
	printk(KERN_INFO "demo_ledtrig: Flashing %d times for string.\n", maxStringLength);

	//blocking call, returns only after flashing done.
	// flash message to LED (loop characters in input, decipher flash code)
	
	//  dot/dash is separated by one dot-time. During this time the LED is off.
	// Two letters in a message are separated by three dot-times. During this time the LED is off.
	// Each break between words is equal to seven dot-times total (no additional 3 dot-time inter-character delay).
	for (i = 0; i < maxStringLength; i++) {
		if (copy_from_user(&kernelBuff[i], &buff[i],sizeof(char))>0){	//loop thru all characters in input
			printk(KERN_INFO "ERROR: Unable to read byte %d",i);
			return -EFAULT;
		}
	}
	printk(KERN_INFO "String copied over to kernel space: |%s|", kernelBuff);

	trimmedString = trimwhitespace(kernelBuff); // removes trailing whitespace from kernelBuff
	strip_extra_spaces(trimmedString); // removes consecutive white spaces in string
	printk(KERN_INFO "Copied %d bytes over to kernelBuff.", count);
	printk(KERN_INFO "String after trimming: |%s|", trimmedString);

	//loop through all characters in the input buffer, determining the flash code for each letter, 
	// and then flashing it out to the LED(s).
	for (i = 0; i < strlen(trimmedString); i ++) {
		deciphered = decipher_led_code(trimmedString[i]);
		convert_to_morse(deciphered);
	}
	// After processing entire message, append a new line to the kfifo
	if (!kfifo_put(&queue, '\n')) {
		return -EFAULT;
	}
	// Return # bytes actually written.
	*ppos += count;
	return count;
}

//TODO
// Called when user space application tries to read the character device
static ssize_t my_read(struct file *file, char *buf, size_t count, loff_t *f_pos)
{
	//fill user buffer with data...
	int bytes_read = 0;

	// need: case where no data available, returns 0 bytes
	// return 0;

	// case where read buffer is smaller than data being read
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