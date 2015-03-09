
/*
 *  V4L2 video capture example
 *
 * Modified by Chad Page for acatcher 
 *
 *  This program can (still) be used and distributed without restrictions.
 *
 *      This program is provided with the V4L2 API
 * see http://linuxtv.org/docs.php for more information
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#include <iostream>
using namespace std;

#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct buffer {
        void   *start;
        size_t  length;
};

void errno_exit(const char *s)
{
        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
}

int xioctl(int fh, int request, void *arg)
{
        int r;

        do {
                r = ioctl(fh, request, arg);
        } while (-1 == r && EINTR == errno);

        return r;
}

class V4L_Capture {
private:
	int  fd;
	char *dev_name;

	struct buffer *buffers;
	unsigned int  n_buffers;

	virtual void ProcessImage()
	{

	}

public:
	int get_fd() {return fd;}

	bool start(void) {
	        unsigned int i;
		enum v4l2_buf_type type;

	        for (i = 0; i < n_buffers; ++i) {
       			struct v4l2_buffer buf;

	                CLEAR(buf);
       		        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                	buf.memory = V4L2_MEMORY_MMAP;
                	buf.index = i;

                	if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) {
                       	    errno_exit("VIDIOC_QBUF");
				return false;
			}
	        }
       		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        	if (-1 == xioctl(fd, VIDIOC_STREAMON, &type)) {
                	errno_exit("VIDIOC_STREAMON");
			return false;
		}

		return true;
	}

	void stop() {
        	enum v4l2_buf_type type;

	        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
       		if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
                	errno_exit("VIDIOC_STREAMOFF");
	}
	
	virtual bool read() {
        	struct v4l2_buffer buf;

       		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
			switch (errno) {
				case EAGAIN:
					return false;

				case EIO:
					/* Could ignore EIO, see spec. */
					/* fall through */

				default:
					close(fd);
					fd = -1;
					return false;
			}
		}

		assert(buf.index < n_buffers);

		cerr << "fd " << fd << " buffer " << buf.bytesused << endl;
		ProcessImage(); //buffers[buf.index].start, buf.bytesused);

		if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) {
			close(fd);
			fd = -1;
			return false;
		}

		return true;
	} 
      
	~V4L_Capture() 
	{ 
		if (fd >= 3) close(fd);

		for (unsigned int i = 0; i < n_buffers; ++i) {
			if (-1 == munmap(buffers[i].start, buffers[i].length))
				fprintf(stderr, "munmap err\n");
		}

		free(buffers);
	}

	V4L_Capture() {
		cerr << "c\n";
		fd = -1;
		dev_name = NULL;
	}

	bool init_mmap(void)
	{
		struct v4l2_requestbuffers req;

		CLEAR(req);

		req.count = 4;
		req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		req.memory = V4L2_MEMORY_MMAP;

		if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
			fprintf(stderr, "%s does not support memory mapping\n", dev_name);
			return false;
		}

		if (req.count < 2) {
			fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
			return false;
		}

		buffers = (struct buffer *)calloc(req.count, sizeof(*buffers));

		if (!buffers) {
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
		}

		for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
			struct v4l2_buffer buf;

			CLEAR(buf);
			buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory      = V4L2_MEMORY_MMAP;
			buf.index       = n_buffers;

			if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
				return false;

			buffers[n_buffers].length = buf.length;
			buffers[n_buffers].start =
				mmap(NULL /* start anywhere */,
				buf.length,
				PROT_READ | PROT_WRITE /* required */,
				MAP_SHARED /* recommended */,
				fd, buf.m.offset);

			if (MAP_FAILED == buffers[n_buffers].start) {
				errno_exit("mmap");
				return false;
        		}
       		}
		return true;
	}

	bool init_device(void)
	{
		struct v4l2_capability cap;
		struct v4l2_cropcap cropcap;
		struct v4l2_crop crop;
		struct v4l2_format fmt;

		if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
			if (EINVAL == errno) {
				fprintf(stderr, "%s is no V4L2 device\n", dev_name);
			} else {
				errno_exit("VIDIOC_QUERYCAP");
			}
			return false;
		}

		if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || 
		    !(cap.capabilities & V4L2_CAP_STREAMING) ||
		    !(cap.capabilities & V4L2_CAP_STREAMING)) {
			fprintf(stderr, "%s is not a streaming video capture device\n", dev_name);
			return false;
		}

		CLEAR(cropcap);
		cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
			crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			crop.c = cropcap.defrect; /* reset to default */

			xioctl(fd, VIDIOC_S_CROP, &crop);
		}

		CLEAR(fmt);
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		/* Preserve original settings as set by v4l2-ctl for example */
		if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt)) {
			errno_exit("VIDIOC_G_FMT");
			return false;
		}

		return init_mmap();
	}

	void close_device(void)
	{
       		if (-1 == close(fd))
			errno_exit("close");

		fd = -1;
		unsigned int i;

		for (i = 0; i < n_buffers; ++i)
			if (-1 == munmap(buffers[i].start, buffers[i].length))
				errno_exit("munmap");

		free(buffers);
	}

	bool open_device(char *_dev_name)
	{
		struct stat st;

		dev_name = _dev_name;	

		if (-1 == stat(dev_name, &st)) {
			fprintf(stderr, "Cannot identify '%s': %d, %s\n", dev_name, errno, strerror(errno));
			return false;
		}

		if (!S_ISCHR(st.st_mode)) {
			fprintf(stderr, "%s is no device\n", dev_name);
			return false;
		}

		fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

		if (-1 == fd) {
			fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno, strerror(errno)); 
			return false;        
		}
		return true;
	}
};

const int MAX_CAMS = 16;
V4L_Capture cap[MAX_CAMS];

void mainloop(void)
{
        while (1 /* count-- > 0 */) {
                for (;;) {
                        fd_set fds;
			int max_fd = 0;
                        struct timeval tv;
                        int r;

                        FD_ZERO(&fds);
			for (int i = 0; i < MAX_CAMS; i++) {
				int _fd = cap[i].get_fd();
				if (_fd >= 0) {
					FD_SET(_fd, &fds);
					if (_fd > max_fd) max_fd = _fd;
				}
			}

                        /* Timeout. */
                        tv.tv_sec = 2;
                        tv.tv_usec = 0;

                        r = select(max_fd + 1, &fds, NULL, NULL, &tv);

                        if (-1 == r) {
                                if (EINTR == errno)
                                        continue;
                                errno_exit("select");
                        }
			
			for (int i = 0; i < MAX_CAMS; i++) {
				int _fd = cap[i].get_fd();
				if ((_fd >= 0) && FD_ISSET(_fd, &fds)) {
					cap[i].read();
				}
			}

//                        if (read_frame())
//                               break;
                        /* EAGAIN - continue select loop. */
                }
        }
}

int main(int argc, char **argv)
{
	for (int i = 0; i < 2; i++) {
		char dev_name[32];
		sprintf(dev_name, "/dev/video%d", i);
		cerr << dev_name << endl;
		cap[i].open_device(dev_name);
		cap[i].init_device();
		cap[i].start();
	}

	mainloop();

	return 0;
}
