/*
awatcher.cxx

Copyright (c) 2015 Chad Page 

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

using namespace cv;
using namespace std;

int baseport = 4000;
const int MAX = 16;

const int BUFLEN = 1024*1024;  // in bytes
//const int ABUFLEN = 1024; // in words

// Yes this is ugly.  :P
bool failure = false;

struct sockin {
	int listener_fd, data_fd;
	struct sockaddr_in serv_addr, cli_addr;
	int id;

	sockin(int _id = 0, int baseport = 5000) {
		id = _id;
		listener_fd = data_fd = -1;
		bzero(&serv_addr, sizeof(serv_addr));
		bzero(&cli_addr, sizeof(cli_addr));
		
		listener_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (listener_fd < 0) {
			cerr << "Couldn't open a listener socket.  Weird.\n";
			failure = true;
			return;
		}

		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(baseport + id);

		if (bind(listener_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) {
			cerr << "ERROR: Couldn't bind to port #" << baseport + id << endl;
			failure = true;
			return;
		}

		listen(listener_fd, 1);
	}
	
	virtual void handle(unsigned char *data, int len, int listener) {
	}
	
	virtual void newconn() {
	}
}; 

struct audio_sockin : public sockin {
	int leftover;

//	uint16_t buf[ABUFLEN];
//	int bufsize;

	audio_sockin(int _id) : sockin(_id, 4000) {
		leftover = -1;
		//bufsize = 0;
	}
	
	virtual void handle(unsigned char *data, int len, int listener) {
		int new_leftover = -1;
		if (len % 2) {
			new_leftover = data[len - 1];
			len--;
		}
		if (listener == id) {
			if (leftover >= 0) {
				write(1, &leftover, 1);
				leftover = -1;
			}
			write(1, data, len);
		}
		leftover = new_leftover;
	}

	// new connection: better not have an odd # output for audio!
	virtual void newconn() {
		leftover = -1;
	}
};

const int IBUFLEN = 2048*1024;  // in bytes

struct image_sockin : public sockin {
	uint8_t buf[IBUFLEN];
	int bufsize;

	image_sockin(int _id) : sockin(_id, 4100) {
		bufsize = 0;
	}

	void showImage(int begin, int end, int listener) {
		if (listener == id) {
			Mat imgbuf = cv::Mat(480, 640, CV_8U, &buf[begin]);
			Mat imgMat = cv::imdecode(imgbuf, CV_LOAD_IMAGE_COLOR);

			if (!imgMat.data) cerr << "reading failed\r\n";
//			cerr << "x " << imgMat.rows << ' ' << imgMat.cols << "\r\n";

			imshow("Display Window", imgMat);
			cerr << "updated\r\n";
			waitKey(2);
		}
	} 
	
	virtual void handle(unsigned char *data, int len, int listener) {
		int begin = -1, end = -1;

		if ((len + bufsize) > IBUFLEN) {
			bufsize = 0;
		} 

		memcpy(&buf[bufsize], data, len);
		bufsize += len;

		for (int i = 0; ((begin == -1) || (end == -1)) && (i < bufsize - 1); i++) {
	//		if (buf[i] == 0xff) cerr << i << ' ' << (int)buf[i + 1] << "\r\n";
			if ((buf[i] == 0xff) && (buf[i + 1] == 0xd8)) begin = i;
			if ((buf[i] == 0xff) && (buf[i + 1] == 0xd9)) end = i;
		}

//		cerr << "A " << bufsize << ' ' << begin << ' ' << end << "\r\n";

		if ((begin >= 0) && (end >= 0)) {
			if (begin > end) {
				memmove(buf, &buf[begin], bufsize - begin);
				bufsize -= begin;  
			} else {
//				cerr << "doshow\r\n";
				showImage(begin, end, listener);
				bufsize = 0;
			}
		}


//		if (listener == id) {
//			if (have_fd3) write(3, data, len);
//		}
	}

	virtual void newconn() {
		bufsize = 0;
	}
};

sockin *s[MAX];

unsigned char buf[BUFLEN];

struct termios oldtermios;

void sigcatch(int sig)
{
	tcsetattr(0, TCSAFLUSH, &oldtermios);
	_exit(0);
} 

bool setrawkbd()
{
	// set keyboard to raw mode + unblocking 
	struct termios newtermios;

	if(tcgetattr(0, &oldtermios) < 0)
		_exit(1);

	newtermios = oldtermios;

	newtermios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	newtermios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	newtermios.c_cflag &= ~(CSIZE | PARENB);

	newtermios.c_cflag |= CS8;

	newtermios.c_oflag &= ~(OPOST);

	newtermios.c_cc[VMIN] = 1;
	newtermios.c_cc[VTIME] = 0;

	if(tcsetattr(0, TCSAFLUSH, &newtermios) < 0)
		_exit(1);
						
	int opts = fcntl(0, F_GETFL);
	if (opts < 0) {
		cerr << "HUH?  fcntl failed\n";
		return false;
	} 
	if (fcntl(0, F_SETFL, opts | O_NONBLOCK) < 0) {
		cerr << "HUH?  fcntl(F_SETFL) failed\n";
		return false;
	} 
	return true;
}

int main(void)
{
	int main_rv = 0;
	int cur_listener = 0;

	int num_sockets = (MAX * 2);
	sockin *s[num_sockets];

#if 0
	// check to see if we have a video socket
	have_fd3 = (fcntl(3, F_GETFD) >= 0);
	if (have_fd3) cerr << "Have video output socket\n";
#endif
//	namedWindow("Display Window", WINDOW_AUTOSIZE );
	namedWindow("Display Window", WINDOW_AUTOSIZE );

	// catch signals
	if (((int)signal(SIGINT,sigcatch) < 0) ||
	    ((int)signal(SIGQUIT,sigcatch) < 0) ||
	    ((int)signal(SIGTERM,sigcatch) < 0)) {
		cerr << "Couldn't set up signal catching.  huh?\n";
		return(1);
	}

	if (!setrawkbd()) {
		goto err_exit;
	} 
	
	for (int i = 0; i < num_sockets; i+=2) {
		s[i] = NULL;
	}

	// init listening sockets
	for (int i = 0; i < num_sockets; i+=2) {
		s[i] = new audio_sockin(i / 2);
		s[i+1] = new image_sockin(i / 2);
	}
	
	if (failure) 		
		goto err_exit;

	// now listen for connections and data
	while (1) {
		int topfd = -1; 
		int rv;
		fd_set readfds, writefds, exceptfds;

		struct timespec t;

		t.tv_sec = 1;
		t.tv_nsec = 0;

		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);

		FD_SET(0, &readfds);

		for (int i = 0; i < num_sockets; i++) {
			if (s[i]->listener_fd >= 0) {
				FD_SET(s[i]->listener_fd, &readfds);	
				if (s[i]->listener_fd >= topfd) topfd = s[i]->listener_fd + 1; 
			}
			if (s[i]->data_fd >= 0) {
				FD_SET(s[i]->data_fd, &readfds);	
				if (s[i]->data_fd >= topfd) topfd = s[i]->data_fd + 1; 
			}
		}

		waitKey(1);
		rv = pselect(topfd, &readfds, &writefds, &exceptfds, &t, NULL);
		if (rv == -1 && errno != EINTR) { 
			cerr << "ERROR: select failed\n";
			goto err_exit;
		}

		if ((rv > 0) && FD_ISSET(0, &readfds)) {
			unsigned char c;

			int rv = read(0, &c, 1);

			if (rv == 0) goto good_exit; 

			if (rv == 1) {
				int old_listener = cur_listener;

				cerr << "Got char " << (int)c << "\r\n"; 
				if (c == 'q') goto good_exit;
				if ((c >= '0') && (c <= '9')) {
					cur_listener = c - '0';
				}
				if ((c >= 'a') && (c <= 'f')) {
					cur_listener = c + 10 - 'a';
				}
				if ((c >= 'A') && (c <= 'F')) {
					cur_listener = c + 10 - 'A';
				}
				if (old_listener != cur_listener) {
					cerr << "Now listening to #" << cur_listener << "\r\n";
				}
			}
		}

		for (int i = 0; (rv > 0) && (i < num_sockets); i++) {
			if (!s[i]) break;

			// check for new connections
			if (FD_ISSET(s[i]->listener_fd, &readfds)) {
				socklen_t len = sizeof(struct sockaddr);
				int newfd = accept(s[i]->listener_fd, (struct sockaddr *)&s[i]->cli_addr, &len);
				if (newfd >= 0) {
					if (s[i]->data_fd < 0) {
						int opts = fcntl(newfd, F_GETFL);

						if (opts < 0) {
							cerr << "HUH?  fcntl failed\n";
							goto err_exit;
						} 

						if (fcntl(newfd, F_SETFL, opts | O_NONBLOCK) < 0) {
							cerr << "HUH?  fcntl(F_SETFL) failed\n";
							goto err_exit;
						} 

						s[i]->data_fd = newfd;
						cerr << "New connection on " << i/2 << " " << ((i % 2) ? "video" : "audio") << "\r\n";
						s[i]->newconn();
					} else {
						cerr << "HUH?  new connection on socket # " << i << endl;
					} 
				} 
			}

			// check for new data
			if (FD_ISSET(s[i]->data_fd, &readfds)) {
				int rv = read(s[i]->data_fd, buf, BUFLEN);

				if (rv == 0) { 
					close(s[i]->data_fd);	
					s[i]->data_fd = -1;
					listen(s[i]->listener_fd, 1);
				} else {
					s[i]->handle(buf, rv, cur_listener);
				}
			}
		}
	};


	err_exit:
		main_rv = 1;

	good_exit:
		tcsetattr(0, TCSAFLUSH, &oldtermios);
		for (int i = 0; i < num_sockets; i++) {
			if (s[i] && s[i]->listener_fd >= 0) {
				close(s[i]->listener_fd);
			}
			if (s[i] && s[i]->data_fd >= 0) {
				close(s[i]->data_fd);
			}
		}

		return main_rv;
}


