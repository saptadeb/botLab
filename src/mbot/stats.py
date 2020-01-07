#!/usr/bin/env python

# Copyright (c) 2017 Adafruit Industries
# Author: Tony DiCola & James DeVito
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
import time

import Adafruit_SSD1306

from PIL import Image
from PIL import ImageDraw
from PIL import ImageFont

import subprocess

import lcm

import thread
import signal
import sys

sys.path.append('../lcmtypes')
from oled_message_t import oled_message_t
from exploration_status_t import exploration_status_t

sys.stdout.flush()

OLED_CHANNEL = "OLED_MESSAGE"
EXPLORATION_STATUS_CHANNEL = "EXPLORATION_STATUS"

V = 16

x = [0, 0, 0, 0]
offset_max = [0, 0, 0, 0]
disp_max = 128
offset_pad = 8

# Raspberry Pi pin configuration:
RST = 4     # on the PiOLED this pin isnt used

lines = ['', '', '', '']

def oled_message_handler(channel, data):
    msg = oled_message_t.decode(data)
    lines[2] = msg.line1
    #lines[3] = msg.line2

def exploration_status_handler(channel, data):
    msg = exploration_status_t.decode(data)
    lines[3] = msg.state

def handle_lcm(lcm_obj):
    try:
        while True:
            lcm_obj.handle()
    except KeyboardInterrupt:
    print("lcm exit!");
        sys.exit()

def main():
    print("stats starting:")

    lc = lcm.LCM("udpm://239.255.76.67:7667?ttl=2")
    lc.subscribe(OLED_CHANNEL, oled_message_handler)
    lc.subscribe(EXPLORATION_STATUS_CHANNEL, exploration_status_handler)

    thread.start_new_thread( handle_lcm, (lc, ) )

    mod_count = 0
    mod_max = 1

    # 128x32 display with hardware I2C:
    disp = Adafruit_SSD1306.SSD1306_128_32(rst=RST)

    def cleanup(*args):
    print("SIGTERM CALLED")

        disp.clear()
        disp.display()
        sys.exit()

    signal.signal(signal.SIGTERM, cleanup)

    # Initialize library.
    disp.begin()

    # Clear display.
    disp.clear()
    disp.display()

    # Create blank image for drawing.
    # Make sure to create image with mode '1' for 1-bit color.
    width = disp.width
    height = disp.height
    image = Image.new('1', (width, height))

    # Get drawing object to draw on image.
    draw = ImageDraw.Draw(image)

    # Draw a black filled box to clear the image.
    draw.rectangle((0,0,width,height), outline=0, fill=0)

    # Draw some shapes.
    # First define some constants to allow easy resizing of shapes.
    padding = -2
    top = padding
    bottom = height-padding

    # Load default font.
    #font = ImageFont.load_default()

    # Alternatively load a TTF font.  Make sure the .ttf font file is in the same directory as the python script!
    # Some other nice fonts to try: http://www.dafont.com/bitmap.php
    font = ImageFont.truetype('arial.ttf', 10)

    try:
        while True:

            # Draw a black filled box to clear the image.
            draw.rectangle((0,0,width,height), outline=0, fill=0)

            # Shell scripts for system monitoring from here : https://unix.stackexchange.com/questions/119126/command-to-display-memory-usage-disk-usage-and-cpu-load
            #cmd = "hostname -I | cut -d\' \' -f1"
        cmd = "ifconfig wlan0 | grep \"inet addr:\" | awk '{ print $2 }' | cut -b 6-"

        try:
                IP = subprocess.check_output(cmd, shell = True )
                
            if(len(IP) == 0):
            IP = "initializing..."
            else:
            IP = IP[0:len(IP)-1]
        except:
            IP = "initializing..."
        
        #averaged over the past minute
            cmd = "top -bn1 | grep load | awk '{printf \"C: %.2f\", $(NF-2)}'"
            CPU = subprocess.check_output(cmd, shell = True )

        cmd = "free -m | awk 'NR==2{printf \"M: %sMB %.2f%%\", $3,$3*100/$2 }'"
            MemUsage = subprocess.check_output(cmd, shell = True )

        #print(lines)              

            # Write two lines of text.      
        lines[0] = IP
        lines[1] = CPU + " " + MemUsage


        for n in range(0,4):

        if disp_max < draw.textsize(lines[n], font=font)[0]:
            offset_max[n] = disp_max - draw.textsize(lines[n], font=font)[0] - offset_pad
        else:
            offset_max[n] = 0

            draw.text((x[n], top+8*n), lines[n], font=font, fill=255)

        if offset_max[n] < 0:
            if x[n] < offset_max[n]:
                x[n] = 0
            elif mod_count == 1:
                x[n] -= V

        mod_count += 1;
        if mod_count > mod_max:
        mod_count = 0;

            # Display image.
            disp.image(image)
            disp.display()
            time.sleep(.1)
        
    except KeyboardInterrupt:
    print("main exception!")
        disp.clear()
    disp.display()

if __name__ == "__main__":
    main()
