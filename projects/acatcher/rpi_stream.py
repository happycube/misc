# adapted from http://petrkout.com/electronics/low-latency-0-4-s-video-streaming-from-raspberry-pi-mjpeg-streamer-opencv/

import cv2
import numpy as np
import sys

#stream=urllib.urlopen('http://192.168.0.193:8080/?action=stream')
stream=sys.stdin
bytes=''
while True:
    r = stream.read(1024)
    if (len(r) == 0):
	exit(0)
    bytes+=r
    a = bytes.find('\xff\xd8')
    b = bytes.find('\xff\xd9')
    if a!=-1 and b!=-1:
        jpg = bytes[a:b+2]
        bytes= bytes[b+2:]
        i = cv2.imdecode(np.fromstring(jpg, dtype=np.uint8),cv2.CV_LOAD_IMAGE_COLOR)
        cv2.imshow('i',i)
        if cv2.waitKey(1) == 27:
            exit(0)

