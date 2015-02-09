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

using namespace std;

int baseport = 4000;
const int MAX = 16;

const int BUFLEN = 1024;  // in bytes
const int ABUFLEN = 1024; // in words

struct sockin {
	int listener_fd, data_fd;
	struct sockaddr_in serv_addr, cli_addr;

	sockin() {
		listener_fd = data_fd = -1;
		bzero(&serv_addr, sizeof(serv_addr));
		bzero(&cli_addr, sizeof(cli_addr));
	}
	
	void handle(unsigned char *data, int len, bool have_stdout) {
	}
}; 

struct audio_sockin : public sockin {
	int leftover;

//	uint16_t buf[ABUFLEN];
//	int bufsize;

	audio_sockin() : sockin() {
		leftover = -1;
		//bufsize = 0;
	}
	
	void handle(unsigned char *data, int len, bool have_stdout) {
		int new_leftover = -1;
		if (len % 2) {
			new_leftover = data[len - 1];
			len--;
		}
		if (have_stdout) {
			if (leftover >= 0) {
				write(1, &leftover, 1);
				leftover = -1;
			}
			write(1, data, len);
		}
		leftover = new_leftover;
	}

	// new connection: better not have an odd # output for audio!
	void newconn() {
		leftover = -1;
	}
};

audio_sockin s[MAX];

unsigned char buf[BUFLEN];

struct termios oldtermios;

void sigcatch(int sig)
{
	tcsetattr(0, TCSAFLUSH, &oldtermios);
	_exit(0);
} 

int main(void)
{
	int main_rv = 0;
	int cur_listener = 0;

	// catch signals
	if (((int)signal(SIGINT,sigcatch) < 0) ||
	    ((int)signal(SIGQUIT,sigcatch) < 0) ||
	    ((int)signal(SIGTERM,sigcatch) < 0)) {
		cerr << "Couldn't set up signal catching.  huh?\n";
		return(1);
	}

	// set keyboard to raw mode + unblocking 
	struct termios newtermios;

	if(tcgetattr(0, &oldtermios) < 0)
		return(-1);

	newtermios = oldtermios;

	newtermios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	newtermios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	newtermios.c_cflag &= ~(CSIZE | PARENB);

	newtermios.c_cflag |= CS8;

	newtermios.c_oflag &= ~(OPOST);

	newtermios.c_cc[VMIN] = 1;
	newtermios.c_cc[VTIME] = 0;

	if(tcsetattr(0, TCSAFLUSH, &newtermios) < 0)
		return(-1);
						
	int opts = fcntl(0, F_GETFL);
	if (opts < 0) {
		cerr << "HUH?  fcntl failed\n";
		goto err_exit;
	} 
	if (fcntl(0, F_SETFL, opts | O_NONBLOCK) < 0) {
		cerr << "HUH?  fcntl(F_SETFL) failed\n";
		goto err_exit;
	} 

	// init listening sockets
	for (int i = 0; i < MAX; i++) {
		s[i].listener_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (s[i].listener_fd < 0) {
			cerr << "Couldn't open a listener socket.  Weird.\n";
			goto err_exit;
		}

		s[i].serv_addr.sin_family = AF_INET;
		s[i].serv_addr.sin_addr.s_addr = INADDR_ANY;
		s[i].serv_addr.sin_port = htons(baseport + i);

		if (bind(s[i].listener_fd, (struct sockaddr *)&s[i].serv_addr, sizeof(s[i].serv_addr))) {
			cerr << "ERROR: Couldn't bind to port #" << baseport + i << endl;
			goto err_exit;
		}

		listen(s[i].listener_fd, 1);
	}

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

		for (int i = 0; i < MAX; i++) {
			if (s[i].listener_fd >= 0) {
				FD_SET(s[i].listener_fd, &readfds);	
				if (s[i].listener_fd >= topfd) topfd = s[i].listener_fd + 1; 
			}
			if (s[i].data_fd >= 0) {
				FD_SET(s[i].data_fd, &readfds);	
				if (s[i].data_fd >= topfd) topfd = s[i].data_fd + 1; 
			}
		}

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

		for (int i = 0; (rv > 0) && (i < MAX); i++) {
			// check for new connections
			if (FD_ISSET(s[i].listener_fd, &readfds)) {
				socklen_t len = sizeof(struct sockaddr);
				int newfd = accept(s[i].listener_fd, (struct sockaddr *)&s[i].cli_addr, &len);
				if (newfd >= 0) {
					if (s[i].data_fd < 0) {
						int opts = fcntl(newfd, F_GETFL);

						if (opts < 0) {
							cerr << "HUH?  fcntl failed\n";
							goto err_exit;
						} 

						if (fcntl(newfd, F_SETFL, opts | O_NONBLOCK) < 0) {
							cerr << "HUH?  fcntl(F_SETFL) failed\n";
							goto err_exit;
						} 

						s[i].data_fd = newfd;
						cerr << "New connection on " << i << "\r\n";
						s[i].newconn();
					} else {
						cerr << "HUH?  new connection on socket # " << i << endl;
					} 
				} 
			}

			// check for new data
			if (FD_ISSET(s[i].data_fd, &readfds)) {
				int rv = read(s[i].data_fd, buf, BUFLEN);

				if (rv == 0) { 
					close(s[i].data_fd);	
					s[i].data_fd = -1;
					listen(s[i].listener_fd, 1);
				} else {
					s[i].handle(buf, rv, (i == cur_listener));
//					rv = write(1, buf, rv);
				}
			}
		}
	};


	err_exit:
		tcsetattr(0, TCSAFLUSH, &oldtermios);
		return(1);

	good_exit:
		tcsetattr(0, TCSAFLUSH, &oldtermios);
		return(0);
}


