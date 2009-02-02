#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>



#define BUFFER_SIZE 1024


static int socket_connect(char *host, in_port_t port){
	struct hostent *hp;
	struct sockaddr_in addr;
	int on = 1, sock;
	if((hp = gethostbyname(host)) == NULL){
		fprintf(stderr,"gethostbyname\n");
		return 0;
	}
	bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);
	addr.sin_port = htons(port);
	addr.sin_family = AF_INET;
	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(int));
	if(sock == -1){
		fprintf(stderr,"setsockopt failed\n");
		return 0;
	}
	if(connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) == -1){
		fprintf(stderr,"connect failed\n");
		return 0;
	}
	return sock;
}

static int http_readline(int fd, char *buffer){ /* not efficient on long lines (multiple unbuffered 1 char reads) */
	int n=0;
	bzero(buffer, BUFFER_SIZE);
	while (n<BUFFER_SIZE-1) {
		if (read(fd,buffer,1)!=1) {
			n= -n;
			break;
		}
		n++;
		if (*buffer=='\015') continue; /* ignore CR */
		if (*buffer=='\012') break;    /* LF is the separator */
		buffer++;
	}
	*buffer=0;
	return n;
}

static int http_read_buffer(int fd, char *buffer, int length){
	int n,r;
	for (n=0; n<length; n+=r) {
		r=read(fd,buffer,length-n);
		if (r<=0) 
			return -n;
		buffer+=r;
	}
	return n;
}

int http_query(char *command, char *host, char *path, int port, char *additional_header, char *data, char *body) {
	int s;
	int status;
	int response_length;
	char buffer[BUFFER_SIZE];
	char *pc;

	if((s = socket_connect(host, port))){
		
		bzero(buffer,BUFFER_SIZE);
		if(data){
			sprintf(buffer,"%s %.256s HTTP/1.0\nContent-length: %d\n\n", command, path, (int)strlen(data));			
		}else{
			sprintf(buffer,"%s %.256s HTTP/1.0\n\n",command, path);
		}
		write(s, buffer, strlen(buffer));  
		// fprintf(stderr, buffer);
		
		if(data){
			write(s,data,strlen(data));
		}
		
		http_readline(s, buffer);
		sscanf(buffer,"HTTP/1.%*d %03d",(int*)&status);;

		while(http_readline(s, buffer)){
			if(*buffer=='\0'){
				break;
			} else {
				// fprintf(stderr, "line:%s\n", buffer);	
				for (pc=buffer; (*pc!=':' && *pc) ; pc++) 
					*pc=tolower(*pc);
				sscanf(buffer,"content-length: %d",&response_length);
			}
		}
		
		*body=0;
		bzero(body,200000);
		if(response_length>0){
			// fprintf(stderr, "body length:%i\n", response_length);
			read(s,body,response_length);
			// fprintf(stderr, "body:%s\n", body);
		}else{
			bzero(buffer,BUFFER_SIZE);
			while(read(s,buffer,BUFFER_SIZE)){
				// fprintf(stderr,"read-%s\n",buffer);
				strncat(body,buffer,BUFFER_SIZE);
				bzero(buffer,BUFFER_SIZE);
			}
		}

		shutdown(s, SHUT_RDWR); 
		close(s);		
		
		return status;
		
	} else {
		return -10;	
	}
}

