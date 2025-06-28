# Linux Kernel Music Device Driver
## Summary:
* The driver uses a character device `/dev/music_dev` which userspace can read (`cat`)/write(`echo`) to play and monitor music playback (done by physical buttons).

### Features:
* Playing tunes from a txt file containing ABC notation with octavs and tempo division (1/2-1/4-1/18-1/16), well known by musicians (excluding sharp and flat tunes).
* Reading button states (Play, Stop, Pause).
* Reading /dev/music_dev returns the current button state as a string like "PLAY\n", "STOP\n", "PAUS\n", or "EXIT\n" when using `cat`.
* The driver detects button state changes and reports them to userspace only when changed.
* The driver manages debouncing for buttons to avoid false triggers.
* The code uses kernel delays (msleep and udelay) to generate tones via toggling GPIO for a piezo buzzer or a passive speaker (C Y type).

## Sheet Music Form:
Each sheet music should contain notes (C, D, E, F, G, A, B) in upper case, each followed by the number of octav (0-8). The character (-) indicates a pause. ANy other configuration or misplaced character or number will casue an error that will only appear upon encountering such strang character.

#### Example: "Jesus bleibet meine Freude" van Johann Sebastian Bach. First verse:
`C4D4E4G4F4-F4A4G4-G4C5B4C5G4E4C4-D4E4F4G4A4G4F4E4D4E4C4-B3C4D4G3B3D4-F4E4D4E4E4`



## Usage:
* Compile and insert the module using `sudo insmod music.ko`.
* Remove with `sudo rmmod music.ko`.
* Check kernel messages with sudo dmesg -W.
* Monitor buttons status by `sudo cat /dev/music_dev` (will appear only if a music piece is playing).

* Set permissions or use sudo for device access to use `echo` command: `sudo chmod 666 /dev/music_dev`.

### Writing to /dev/music_dev supports two modes:
* Writing two integers (freq duration) plays a single tone immediately.
* Writing a sheet music string with note names and octaves plays a sequence of notes.
### Example:
```bash
echo "440 1000" > /dev/music_dev  # play 440 HZ for 1000ms
echo "$(cat bach.txt) 4" > /dev/music_dev  # play sheet music in "bach.txt" with L=1/4 base note length (L is 2 sec)
```

## Device Cleanup:
The following cleanups are called at the end of the code or whenever a relevant process fails:
* Frees GPIO pins.
* Deletes character device.
* Destroys device and class.
* Unregisters device numbers.

## Buttons functions:
* Pause: pauses the current melody, pressing play will continues from the last note.
* Play: does not affect the melody unles it was paused or stopped.
* Stop: pressing once will stop the melody, pressing play after that will start the melody from the begining. Pressing Stop again will exit the melody, the user has to use `echo` command again to load a melody.

## Remarks/Issues:
* Button states (play, stop, pause) are stored as `volatile` flags: ply, stp, pau. Not using the `volatile` syntax xaused the states not to be read in realtime inside the `device_write` function.
* To allow the cat command to keep reading from the device without reaching an EOF (end-of-file) and stopping, the device_read function uses a trick:

    * *No EOF:* The `device_read` function never signals an end-of-file, so cat keeps trying to read.

    * *Avoid repeating the same output:* Normally, if you return without EOF, cat will print whatever in the buffer repeatedly. So:

    * *Initial data copy*: The first time `device_read` runs, it saves the initial content of the user buffer by copying it with `copy_from_user`.

    * *Showin output at will:* When you actually want to print something to the terminal, I call `copy_to_user` with the new data to send. Then, on the next call to device_read, it will resend the saved initial buffer content (which looks unchanged), forcing cat to wait again after printing the new data.

* Could not configure input_pull_up resistors, instead, they are implemented in the hardware.
