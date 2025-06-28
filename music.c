/*
make
sudo insmod music.ko #to insert it
sudo rmmod music.ko # to stop it

on another terminal:
sudo dmesg -W # to show it working
lsmod #to list all modules running

sudo cat /dev/music_dev # becasue I'm counting on the read function, should find another
echo 0 | sudo tee /dev/music_dev // to avoid permission denied, or:
sudo chmod 666 /dev/music_dev

echo "1 1000"  > /dev/music_dev
echo "$(cat bach.txt) 1000" > /dev/music_dev


*/
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h> //need the simplest thing

//music notes
//https://mixbutton.com/music-tools/frequency-and-pitch/music-note-to-frequency-chart#1st
int C[]  = {16, 33, 65, 131, 261, 523, 1046, 2093, 4186};
int Cs[] = {17, 35, 69, 138, 277, 554, 1109, 2217, 4435};  // C# / Db
int D[]  = {18, 37, 73, 147, 294, 587, 1175, 2349, 4699};
int Ds[] = {19, 39, 78, 156, 311, 622, 1245, 2489, 4978};  // D# / Eb
int E[]  = {21, 41, 82, 165, 330, 659, 1319, 2637, 5274};
int F[]  = {22, 44, 87, 175, 349, 698, 1397, 2794, 5588};
int Fs[] = {23, 46, 93, 185, 370, 740, 1480, 2960, 5920};  // F# / Gb
int G[]  = {25, 49, 98, 196, 392, 784, 1568, 3136, 6272};
int Gs[] = {26, 52, 104, 208, 415, 831, 1661, 3322, 6645}; // G# / Ab
int A[]  = {27, 55, 110, 220, 440, 880, 1760, 3520, 7040};
int As[] = {29, 58, 117, 233, 466, 932, 1865, 3729, 7459}; // A# / Bb
int B[]  = {31, 62, 123, 247, 494, 988, 1975, 3951, 7902};
//

#define BTN_PLAY_GPIO   17
#define BTN_STOP_GPIO  27
#define BTN_PAUSE_GPIO  5

#define SPKR_GPIO      22
#define IO_OFFSET     512

#define debounce 50

#define DEVICE_NAME   "music_dev"
#define CLASS_NAME    "music_class"

//following this method instead of register_chrdev(0, "cde_name", fops) to allow creating the device here rather than from mknod in terminal
static dev_t music_dev_num; //device number type, both major and minor // MAJOR(dev_t), MINOR(dev_t)  to get the numbers
static struct class *music_dev_class;
static struct cdev music_cdev; //cdev_init(&music_cdev, &fops);  // cdev_add(&music_cdev, music_dev_num, 1); 

//global control 
volatile char stp = 0;
volatile char ply = 0;
volatile char pau = 0;
volatile char is_playing = 0;


//debounce function:
static int is_pressed(int pin_num)
{
    if (gpio_get_value(pin_num + IO_OFFSET)) {
        msleep(debounce);
        if (gpio_get_value(pin_num + IO_OFFSET)) {
            return 1; 
        }
    }
    return 0; 
}


// File operations 
static ssize_t device_read(struct file *file, char __user *buf, size_t len, loff_t *offset) {

    //trying to trick user space:
    static int first_check = 1;
    char internal_buf[10];
    if(first_check){
        if (copy_from_user(internal_buf, buf, 10)) {
        pr_err("Failed to copy data from user\n");
        return -EFAULT;
        }
        else first_check = 0;
    }
    else {
        if (copy_to_user(buf, internal_buf, 10)) return -EFAULT;
    }
    // Works, very dangerous, but I'm evil >:D

    char state[5];
    //static int val=0;
    static int PU_prev_state = 0;
    static int STP_prev_state = 0;
    static int PLY_prev_state = 0;

    //if (!gpio_get_value(BTN_STOP_GPIO + IO_OFFSET) && gpio_get_value(BTN_PLAY_GPIO + IO_OFFSET)) val =1;
    //if (gpio_get_value(BTN_STOP_GPIO + IO_OFFSET)) val =0;

    if (!is_playing){
        stp = 0; ply = 0; pau = 0;
    }
    else {
        if (is_pressed(BTN_STOP_GPIO ) && stp ==0) {
            state[0] ='S'; state[1] ='T'; state[2] ='O'; state[3] ='P'; //much safer :'(
            stp = 1; ply = 0; pau = 0;
        }
        else if (is_pressed(BTN_STOP_GPIO ) && stp ==1 && is_playing) {
            state[0] ='E'; state[1] ='X'; state[2] ='I'; state[3] ='T'; //much safer :'(
            stp = 2; ply = 0; pau = 0;
        }
        else if (is_pressed(BTN_PAUSE_GPIO ) ) {
            state[0] ='P'; state[1] ='A'; state[2] ='U'; state[3] ='S';
            stp = 0; ply = 0; pau = 1;
        }
        else if (is_pressed(BTN_PLAY_GPIO ) ) {
            state[0] ='P'; state[1] ='L'; state[2] ='A'; state[3] ='Y';
            stp = 0; ply = 1; pau = 0;
        }
    }
    state[4] = '\n';
    
    //gpio_set_value(SPKR_GPIO + IO_OFFSET, val);

    if (gpio_get_value(BTN_STOP_GPIO + IO_OFFSET) != STP_prev_state
        || gpio_get_value(BTN_PLAY_GPIO + IO_OFFSET) != PLY_prev_state
        || gpio_get_value(BTN_PAUSE_GPIO + IO_OFFSET) != PU_prev_state){

            if (copy_to_user(buf, state, 5)) return -EFAULT;
            STP_prev_state = gpio_get_value(BTN_STOP_GPIO + IO_OFFSET);
            PLY_prev_state = gpio_get_value(BTN_PLAY_GPIO + IO_OFFSET);  
            PU_prev_state = gpio_get_value(BTN_PAUSE_GPIO + IO_OFFSET);         

    }
    return 5;
}

static void send_tone(int freq, int duration){
    
    if (freq == 0){
        gpio_set_value(SPKR_GPIO + IO_OFFSET, 0);
        msleep(duration);
        return;
    }
    //https://forum.arduino.cc/t/how-to-program-piezo-buzzer-without-the-tone-library/327434/9
    // Tone generation
    
    int half_period_us = 1000000 / (freq * 2);
    int cycles = (freq * duration) / 1000;


        while (cycles-- > 0) {
            gpio_set_value(SPKR_GPIO + IO_OFFSET, 1);  
            udelay(half_period_us);
            gpio_set_value(SPKR_GPIO + IO_OFFSET, 0);
            udelay(half_period_us);
        }
    
}

static ssize_t device_write(struct file *file, const char __user *buf, size_t len, loff_t *offset) {
    printk("Starting song. ply:%d, stp:%d, pau:%d.\n", ply, stp, pau);
    
    int duration = 0;
    int freq = 0;
    char input[1024];
   // char sheet_music[1024];
    
    int one_note_len = 2000;//in ms, length of 1/1 
    int end_of_sheet = len;

    if (len > sizeof(input) - 1)
        return -EINVAL;

    if (copy_from_user(input, buf, len))
        return -EFAULT;


    if (sscanf(input, "%d %d", &freq, &duration) == 2) //if user wants freq and duration
    {
        send_tone(freq,duration);
        return len;
    }

    //else, it is a sheet music 
    
    input[len] = '\0'; //null terminate the input
    if (input[len-2]>='1' && input[len-3]==' '){
        duration = input[len-2]-'0';
        end_of_sheet = len-3;
    }
    else if (input[len-2]=='6' && input[len-3]=='1' && input[len-4]==' '){
        duration = 16;
        end_of_sheet = len-4;
    }
    if (duration!=1 && duration!=2 && duration!=4 && duration!=8 && duration!=16) {
        pr_err("Wrong note divisor: %d\n", duration);
        return -EINVAL;
    }

    is_playing = 1;
    int octav =0;
    for (size_t i = 0; i<end_of_sheet; i++){
        while (stp==1){ //dangerous??
            i=0;           

        }
        if (stp==2) {
                stp = 0; ply = 0; pau=0; is_playing = 0;
                
                return len;
            } //2 stop presses exits
        
        while (pau){ // also dangerous??

        }
        switch (input[i]) {
            case 'A':
                octav = input[i+1]-'0';
                if(octav<0 || octav > 8) {
                    is_playing = 0;
                    pr_err("Wrong note octav: %c%d at index %zu\n", input[i], octav, i);
                    return -EINVAL;
                }
                send_tone(A[octav],one_note_len/duration);
                i++; //skip the next char, it i already the octav
                break;

            case 'B':
                octav = input[i+1]-'0';
                if(octav<0 || octav > 8) {
                    is_playing = 0;
                    pr_err("Wrong note octav: %c%d at index %zu\n", input[i], octav, i);
                    return -EINVAL;
                }
                send_tone(B[octav],one_note_len/duration);
                i++;
                break;

            case 'C':
                octav = input[i+1]-'0';
                if(octav<0 || octav > 8) {
                    is_playing = 0;
                    pr_err("Wrong note octav: %c%d at index %zu\n", input[i], octav, i);
                    return -EINVAL;
                }
                send_tone(C[octav],one_note_len/duration);
                i++;
                break;

            case 'D':
                octav = input[i+1]-'0';
                if(octav<0 || octav > 8) {
                    is_playing = 0;
                    pr_err("Wrong note octav: %c%d at index %zu\n", input[i], octav, i);
                    return -EINVAL;
                }
                send_tone(D[octav],one_note_len/duration);
                i++;
                break;

            case 'E':
                octav = input[i+1]-'0';
                if(octav<0 || octav > 8) {
                    is_playing = 0;
                    pr_err("Wrong note octav: %c%d at index %zu\n", input[i], octav, i);
                    return -EINVAL;
                }
                send_tone(E[octav],one_note_len/duration);
                i++;
                break;

            case 'F':
                octav = input[i+1]-'0';
                if(octav<0 || octav > 8) {
                    is_playing = 0;
                    pr_err("Wrong note octav: %c%d at index %zu\n", input[i], octav, i);
                    return -EINVAL;
                }
                send_tone(F[octav],one_note_len/duration);
                i++;
                break;

            case 'G':
                octav = input[i+1]-'0';
                if(octav<0 || octav > 8) {
                    is_playing = 0;
                    pr_err("Wrong note octav: %c%d at index %zu\n", input[i], octav, i);
                    return -EINVAL;
                }
                send_tone(G[octav],one_note_len/duration);
                i++;
                break;

            case '-':
                send_tone(0,one_note_len/(8*duration));
                break;
            default:
                is_playing = 0;
                pr_err("Wrong or misplaced character: %c at index %zu\n", input[i], i);
                return -EINVAL;
                
        }


    }
    is_playing = 0;
    return len;
}

static struct file_operations fops = {
    .owner = THIS_MODULE, // doesn't allow rmmod while the file is open(any of the functions is running) //safe and so
    .read = device_read,
    .write = device_write,
};

// Module initialization
static int __init music_dev_init(void) {
    //long process, but recommended
    //https://embetronicx.com/tutorials/linux/device-drivers/device-file-creation-for-character-drivers/

    // Allocate device number
    if (alloc_chrdev_region(&music_dev_num, 0, 1, DEVICE_NAME) < 0) { //allocates major/minor in music_dev_num, minor start 0, 1 device
        printk(KERN_ERR "Failed to allocate device number\n");
        return -1;
    }

    // Create device class
    music_dev_class = class_create(CLASS_NAME); //the device family, if I have several devices here I can add them all to one class and tell the user_space to create that device_family once (next step)
    if (IS_ERR(music_dev_class)) {
        unregister_chrdev_region(music_dev_num, 1);// safely clean, unregister 1 device
        printk(KERN_ERR "Failed to create device class\n");
        return -1;
    }

    // Create device file
    //tells the user_space "create that device with that name inside /dev/"
    if (device_create(music_dev_class, NULL, music_dev_num, NULL, DEVICE_NAME) == NULL) { //device_create(struct class *class, struct device *parent, dev_t dev_num, void *drvdata, const char *fmt)
        class_destroy(music_dev_class);
        unregister_chrdev_region(music_dev_num, 1);
        printk(KERN_ERR "Failed to create device file\n");
        return -1;
    }

    // Initialize and add character device
    cdev_init(&music_cdev, &fops); //tell that device "you have that file"
    if (cdev_add(&music_cdev, music_dev_num, 1) < 0) {
        device_destroy(music_dev_class, music_dev_num);
        class_destroy(music_dev_class);
        unregister_chrdev_region(music_dev_num, 1);
        printk(KERN_ERR "Failed to add character device\n");
        return -1;
    }

    // GPIOs setup 
    if (gpio_request(SPKR_GPIO + IO_OFFSET, "SPKR_GPIO") < 0) {
        printk(KERN_ERR "Failed to request SPEAKER GPIO\n");
        cdev_del(&music_cdev);
        device_destroy(music_dev_class, music_dev_num);
        class_destroy(music_dev_class);
        unregister_chrdev_region(music_dev_num, 1);
        return -1;
    }
    gpio_direction_output(SPKR_GPIO + IO_OFFSET, 0);

    if (gpio_request(BTN_PLAY_GPIO + IO_OFFSET, "BTN_PLAY_GPIO") < 0) {
        printk(KERN_ERR "Failed to request PLAY button GPIO\n");
        gpio_free(BTN_PLAY_GPIO + IO_OFFSET);
        cdev_del(&music_cdev);
        device_destroy(music_dev_class, music_dev_num);
        class_destroy(music_dev_class);
        unregister_chrdev_region(music_dev_num, 1);
        return -1;
    }
    gpio_direction_input(BTN_PLAY_GPIO + IO_OFFSET);

    if (gpio_request(BTN_STOP_GPIO + IO_OFFSET, "BTN_STOP_GPIO") < 0) {
        printk(KERN_ERR "Failed to request STOP button GPIO\n");
        gpio_free(BTN_STOP_GPIO + IO_OFFSET);
        cdev_del(&music_cdev);
        device_destroy(music_dev_class, music_dev_num);
        class_destroy(music_dev_class);
        unregister_chrdev_region(music_dev_num, 1);
        return -1;
    }
    gpio_direction_input(BTN_STOP_GPIO + IO_OFFSET);

    if (gpio_request(BTN_PAUSE_GPIO + IO_OFFSET, "BTN_STOP_GPIO") < 0) {
        printk(KERN_ERR "Failed to request PAUSE button GPIO\n");
        gpio_free(BTN_PAUSE_GPIO + IO_OFFSET);
        cdev_del(&music_cdev);
        device_destroy(music_dev_class, music_dev_num);
        class_destroy(music_dev_class);
        unregister_chrdev_region(music_dev_num, 1);
        return -1;
    }
    gpio_direction_input(BTN_STOP_GPIO + IO_OFFSET);

    printk(KERN_INFO "Music driver started successfully\n");
    return 0;
}

// Module cleanup
static void __exit music_dev_exit(void) {
    // Free GPIOs
    gpio_free(SPKR_GPIO + IO_OFFSET); //so the pi reserves each GPI, have to free them, otherwise I don't know how it will behave if I remove the device and add it again.
    gpio_free(BTN_PLAY_GPIO + IO_OFFSET);
    gpio_free(BTN_STOP_GPIO + IO_OFFSET);
    gpio_free(BTN_PAUSE_GPIO + IO_OFFSET);

    // Cleanup device
    cdev_del(&music_cdev);
    device_destroy(music_dev_class, music_dev_num);
    class_destroy(music_dev_class);
    unregister_chrdev_region(music_dev_num, 1);

    printk(KERN_INFO "Music driver exiting\n");
}

module_init(music_dev_init);
module_exit(music_dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter 545727");
MODULE_DESCRIPTION("Music driver using GPIOs with file operations");
