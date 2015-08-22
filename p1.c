#include <sys/select.h>
#include <stdio.h>
#include <unistd.h> 
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <malloc.h>
#include <errno.h>
#include <wait.h>
#include <glib.h>

#define false 0
#define true 1
#define READ 0
#define WRITE 1
#define RECV_OK    		0
#define RECV_EOF  		-1
#define DEFAULT_BACKLOG		5
#define NON_RESERVED_PORT 	8854
#define BUFFER_LEN	 	2000	
#define SERVER_ACK		"ACK_FROM_SERVER"

/* Macros to convert from integer to net byte order and vice versa */
#define i_to_n(n)  (int) htonl( (u_long) n)
#define n_to_i(n)  (int) ntohl( (u_long) n)

void server(int port);
int setup_to_accept(int port)	;
int accept_connection(int default_socket);	
void send_phonebook(int new_socket, char *phonebook[], int numProcs);
char *arg_cat(int argc, char *argv[], int argstart);
int send_msg(int fd, char *buf, int size);	
void insert_id(char *buf, int rank);

const LLEN = 256;

int main(int argc, char *argv[])
{
	
	FILE *infile, *fp[LLEN];
	int i, j, k, len, rc, closed = 0;
	int numProcs = 1, numHosts;
	int lineNum = 0, debug = 0, port;
	int default_socket, new_socket;
	int sock_ind = 0;
	int read_fds[LLEN];
	int sock_read_fds[LLEN];
	int fds[LLEN];
	int sock_fds[LLEN];
	char *phonebook[LLEN];
	char buff[2000];
	char env[256];
	char ho_name[LLEN][LLEN];
	char line[LLEN];
	char hostname[50];
	char *args;
	fd_set readSet;
	fd_set sockReadSet;
	struct timeval tv;


	setbuf(stdout,NULL);	
	
	gethostname(hostname, 50);
	
	if (argc > 0)
	{
		while ( (rc = getopt(argc, argv, "ldn:p:")) != -1) 
		{
			switch (rc) 
			{
				case 'l':
					lineNum = 1;
					break;
				case 'n':
					numProcs = atoi(optarg);
					break;
				case 'p':
					port = atoi(optarg);
					break;
				case 'd':
					debug = 1;
					break;
				case '?':
					break;
				default:
					printf ("?? getopt returned character code 0%o ??\n",  rc);
			}
		}
    
		if (optind < argc) 
		{
			args = arg_cat(argc, argv, optind);
		}
	}
	
	
	//initialize line buffer and hostname matrix
	for(i = 0; i < LLEN; i++)
	{
		for (j = 0; j < LLEN; j++)
		{
			ho_name[i][j] = '\0';			
		}
	}
	for (i = 0; i < LLEN; i++)
	{
		line[i] = '\0';
		read_fds[i] = 0;
		sock_read_fds[i] = 0;
	}

	//Open hostnames file and read in names
	if ((infile = fopen("hostnames", "r")) == NULL)
	{
		printf("open failed for file: %s\n",argv[1]);
		exit(99);
	}
	i = numHosts = 0;
	while (fgets(line, sizeof line, infile) != NULL)
	{
		len = strlen(line);
		if (line[len-1] == '\n')
		{
			line[len-1] = '\0';
		}
		strcpy(ho_name[i], line);
		i++;
	}
	numHosts = i;

	//create string from command and popen it 
	for (i = 0; i < numProcs; i++)
	{
		sprintf(env," 'export DEBUG=%d;export MPI_RANK=%d;export MPI_SIZE=%d;export SERVER_NAME=%s;export PORT=%d; %s'", 
			    debug, i, numProcs, hostname, port, args);
		sprintf(buff, "ssh %s %s 2>&1", ho_name[i % (numHosts)], env);
		phonebook[i] = malloc(strlen(ho_name[i % numHosts]));
		strcpy(phonebook[i], ho_name[i % numHosts]);
		fp[i] = popen(buff, "r");
		fds[i] = fileno(fp[i]);
		if (debug)
			printf("%s\n", buff); 

	}

	if (debug)
		printf("...popen()s completed\n");
		
	default_socket = setup_to_accept(port);	
	FD_ZERO(&sockReadSet);	


	//prepare the fd_set
	FD_ZERO(&readSet);	
	for (i = 0; i  < numProcs; i++)
	{
		FD_SET(fds[i], &readSet);
	}	
	FD_SET(default_socket, &readSet);
	tv.tv_sec =  0;
	tv.tv_usec = 0;
			
	//exec the programs on server
	rc = select(FD_SETSIZE, &readSet, NULL, NULL, &tv);
//	is_set(numProcs, fds, readSet);		
	while (closed < numProcs)
	{
		for (i = 0; i  < numProcs ; i++)
		{
			if (FD_ISSET(fds[i], &readSet) == 1)
			{
				j = read(fds[i], buff, BUFFER_LEN);
				if (j < 0)
				{

				}
				else if (j == 0)
				{						
					pclose(fp[i]);
					read_fds[i] = 1;
					closed++;
				}
				else
				{
					if (j < BUFFER_LEN)
						buff[j] = '\0';
					
					if (lineNum)
						insert_id(buff, i);
					else
						printf("%s", buff);
				}
			}
			if (FD_ISSET(default_socket, &readSet) == 1)
			{
				new_socket = accept_connection(default_socket);
				
				sock_fds[sock_ind] = new_socket;
				sock_ind++;
			}
		}
		
		FD_ZERO(&readSet);
		for (i = 0; i < numProcs; i++)
		{
			if (read_fds[i] == 0)
				FD_SET(fds[i], &readSet);
		}
		FD_SET(default_socket, &readSet);
		tv.tv_sec =  0;
		tv.tv_usec = 0;
			
		rc = select(FD_SETSIZE, &readSet, NULL, NULL, &tv);
//		is_set(numProcs, fds, readSet);		
				
		for (i = 0; i < sock_ind; i++)
		{
			if (FD_ISSET(sock_fds[i], &sockReadSet) == 1)
			{
				j = read(sock_fds[i], buff, BUFFER_LEN);
				if (strcmp(buff,"phonebook_req") == 0)
				{
					if ((rc = fork()) == -1) //Error Condition 
					{
						printf("server: fork failed\n");
						exit(99);
					}
					else if (rc > 0)  // Parent Process, i.e. rc = child's pid 
					{
						sock_read_fds[i] = 1;
						close(sock_fds[i]);
					}
					else          // Child Process, rc = 0 
					{
						close(default_socket);
						send_phonebook(sock_fds[i], phonebook, numProcs);
						exit(0);
					}					
				}				
			}
		}
		
		FD_ZERO(&sockReadSet);
		for (i = 0; i < sock_ind; i++)
		{
			if (sock_read_fds[i] == 0)
				FD_SET(sock_fds[i], &sockReadSet);
		}
		tv.tv_sec =  0;
		tv.tv_usec = 0;
		
		//is_set(sock_ind, sock_fds, sockReadSet);
		rc = select(FD_SETSIZE, &sockReadSet, NULL, NULL, &tv);
	}
}	

//debugging function - shows the status of all file descriptors in a fd_set
is_set(int numProcs, int fds[], fd_set readSet)
{
	int i;
	
	for (i = 0; i < numProcs; i++)
	{
		printf("fd%d = ", fds[i]);
		if(FD_ISSET(fds[i], &readSet))
		{
			printf("true");
		}
		else
		{
			printf("false");
		}
		printf("\n");
	}
}



char *arg_cat(int argc, char *argv[], int argstart)
{
	int length = 0;
	int i;

	for (i = argstart; i < argc; ++i)
    	length += strlen(argv[i]);

	char *output = (char*)malloc(length + 1);

	char *dest = output;
	for (i = argstart; i < argc; ++i) 
	{
    	char *src = argv[i];
	    while (*src)
        *dest++ = *src++;
	    if (i < argc - 1)
			*dest++ = ' ';
	}
	dest = '\0';
	
	return output;

}

//adds the process rank to before each line of output
void insert_id(char *buf, int rank)
{
	int i;	
	char init[7];
	GString  *str;
	
	sprintf(init, "[%d]: ", rank);
	str = g_string_new(buf);
		
	g_string_prepend(str, init);

	i=0;
	while(i < str->len)
	{
		if(str->str[i]  == '\n' && str->str[i + 1] != '\0')
		{
			g_string_insert(str, i+1,init);			
		}
	
		//if (str->str[i]  == '\n' && str->str[i + 1] != '\n')
			//printf("@@@");
		
		
		i++;
	}			

	printf("%s", str->str);
	if(str->str[str->len - 1] != '\n')
		printf("\n");

	//printf("***");
	//strcpy(buf, str->str);
	g_string_free(str, 1);  	
}

int setup_to_accept(int port)	
{
    int rc, default_socket;
    struct sockaddr_in sin, from;
    int optvalue = 1;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    default_socket = socket(PF_INET, SOCK_STREAM, 0);
    error_check(default_socket,"setup_to_accept socket");

    rc = setsockopt(default_socket, SOL_SOCKET, SO_REUSEADDR, (char*) &optvalue, sizeof(optvalue));	
    error_check(rc, "SockOpt");	
	
    rc = bind(default_socket, (struct sockaddr *)&sin ,sizeof(sin));
    error_check(rc,"setup_to_accept bind");		
	
    rc = listen(default_socket, DEFAULT_BACKLOG);
    error_check(rc,"setup_to_accept listen");
	
	return(default_socket);
}

int accept_connection(int default_socket )	
{
    int fromlen, new_socket, gotit;
    int optval = 1,optlen;
    struct sockaddr_in from;

    fromlen = sizeof(from);
    gotit = 0;
    while (!gotit)
    {
		new_socket = accept(default_socket, (struct sockaddr *)&from, &fromlen);
		if (new_socket == -1)
		{
		    /* Did we get interrupted? If so, try again */
		    if (errno == EINTR)
				continue;
		    else
				error_check(new_socket, "accept_connection accept");
		}
		else
		    gotit = 1;
   	}
    setsockopt(new_socket,IPPROTO_TCP,TCP_NODELAY,(char *)&optval,sizeof(optval));
    return(new_socket);
}

void send_phonebook(int new_socket, char *phonebook[], int numProcs)
{
	int i, ack_len, len;
	char buf[100];
		
	ack_len = strlen(SERVER_ACK) + 1;

	//send number of processes and recv ack
	len = sprintf(buf, "%d", numProcs) + 1;
	i =	send_msg(new_socket, buf, len);
	recv_msg(new_socket, buf);



	//send numProcs
	for (i = 0; i < numProcs; i++)
	{
		sprintf(buf, "%s", phonebook[i]);
		send_msg(new_socket, buf, strlen(buf)+1);
		recv_msg(new_socket, buf);
	}


}

error_check(int val, char *str)	
{
    if (val < 0)
    {
	printf("%s :%d: %s\n", str, val, strerror(errno));
	exit(1);
    }
}

int send_msg(int fd, char *buf, int size)	
{
    int n;

    n = write(fd, buf, size);
    error_check(n, "send_msg write");
	return n;
}

int recv_msg(int fd, char *buf)
{
    int bytes_read;

    bytes_read = read(fd, buf, BUFFER_LEN);
    error_check( bytes_read, "serv recv_msg read");
    if (bytes_read == 0)
	return(RECV_EOF);
    return( bytes_read );
}
