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
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>

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
}; 

struct audio_sockin : public sockin {
	int leftover;

	uint16_t buf[ABUFLEN];
	int bufsize;

	audio_sockin() : sockin() {
		leftover = -1;
		bufsize = 0;
	}
	
	void handle(unsigned char *data, int len, bool have_stdout) {
		if (have_stdout) {
			if (leftover >= 0) {
				write(1, &leftover, 1);
				leftover = -1;
			}
			write(1, data, len);
		}
	}
};

audio_sockin s[MAX];

unsigned char buf[BUFLEN];

int main(void)
{
	// init listening sockets
	for (int i = 0; i < MAX; i++) {
		s[i].listener_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (s[i].listener_fd < 0) {
			cerr << "Couldn't open a listener socket.  Weird.\n";
			return(1);
		}

		s[i].serv_addr.sin_family = AF_INET;
		s[i].serv_addr.sin_addr.s_addr = INADDR_ANY;
		s[i].serv_addr.sin_port = htons(baseport + i);

		if (bind(s[i].listener_fd, (struct sockaddr *)&s[i].serv_addr, sizeof(s[i].serv_addr))) {
			cerr << "ERROR: Couldn't bind to port #" << baseport + i << endl;
			return(1);
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

		for (int i = 0; i < MAX; i++) {
			if (s[i].listener_fd >= 0) {
				FD_SET(s[i].listener_fd + 1, &readfds);	
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
			return(1);
		}

		for (int i = 0; (rv > 0) && (i < MAX); i++) {
			// check for new connections
			if (FD_ISSET(s[i].listener_fd, &readfds)) {
				socklen_t len = sizeof(struct sockaddr);
				int newfd = accept(s[i].listener_fd, (struct sockaddr *)&s[i].cli_addr, &len);
				if (newfd >= 0) {
					if (s[i].data_fd < 0) {
						int opts;

						opts = fcntl(newfd, F_GETFL);
						if (opts < 0) {
							cerr << "HUH?  fcntl failed\n";
							return(1);	
						} 
						if (fcntl(newfd, F_SETFL, opts | O_NONBLOCK) < 0) {
							cerr << "HUH?  fcntl(F_SETFL) failed\n";
							return(1);	
						} 

						s[i].data_fd = newfd;
						cerr << i << ' ' << s[i].data_fd << endl;
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
					handle(buf, rv, true);
//					rv = write(1, buf, rv);
				}
			}
		}
	};


	return 0;
}


