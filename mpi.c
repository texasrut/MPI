#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <glib.h>
#include "mpi.h"

//Communication defs
#define PORT_CALC 		(EXEC_PORT + 1)
#define DEFAULT_BACKLOG	5
#define CLIENT_ACK      "ACK_FROM_CLIENT"
#define BUFFER_LEN	 	1024
#define RECV_EOF  		-1
#define NA				-999

//System Messages
#define NON_SYS				0
#define RECV_OK    			1
#define HANDSHAKE			2
#define	ISEND				3
#define SEND_REQ			4
#define OK_TO_SEND			5
#define BARRIER				6
#define BARRIER_GO			7
#define ABORT				8
#define	BCAST				9
#define IRECV				10
#define	GATHER				11
#define GATHER_NUM			12
#define WIN_LOCK			13
#define WIN_UNLOCK			14
#define LOCK_SUCCESS		15
#define LOCK_FAILURE		16
#define UNLOCK_SUCCESS		17
#define UNLOCK_FAILURE		18
#define PUT					19
#define GET_SEND			20
#define GET_RECV			21
#define DUP_ATTR			22
#define DUPED				23
#define PUT_ATTR			24
#define PUT_ATTR_DONE		25
#define GET_ATTR			26
#define RET_ATTR			27
	
//win info
struct w
{
	int handle;
	int lock;
	int locked_by;
	void *base;
	MPI_Aint size;
	int disp_unit;
	MPI_Info info;
	MPI_Comm comm;
	GQueue *wait_q;

};
typedef struct w wnd;

//Envelope Structure
struct m
{
	int source;
	int dest;
	int tag;
	int comm;
	int sys;
	int size;
	void *msg;
};
typedef struct m msg;

//Communicator Structure
struct cmr
{
	int size;
	int rank;
	int parent;
	int lchild;
	int rchild;
};
typedef struct cmr communicator;
communicator C;

//prototypes
void *sub(void *arg);
GFunc print_msg_data(gpointer data, gpointer user_data);
int tsprintf(const char* format, ...);
msg *queue_find(int *GLOBAL, msg *msgPtr);
msg *asynch_queue_find(int *GLOBAL, msg *msgPtr);
wnd *wnd_find(MPI_Win win);

//lists and queues
GQueue *MSG_Q;
GQueue *WNDS;

//Init Data
char EXEC_SERVER_NAME[50];
int EXEC_PORT;
//char **PHONEBOOK;
int MPI_RANK;
int LOCAL_PORT;
int NUMPROCS;
int DEFAULT_SOCKET;
int RC_TABLE[150];
int SC_TABLE[150];
int DEBUG;
struct timeval timev;//for debugging func, tsprintf

//thread shared variables
char PHONEBOOK[150][150];
int REQ_HANDLES[1000];
int WIN_HANDLES[1000];
int ATTR[500][500];
int REQ_INDEX =				0;
int WIN_INDEX =				0;
int HANDLES_IND =			1; //MPI_COMM_WORLD is 0
int ACT_ISENDS =			0;
int ACT_SNDREQ_ACKS =		0;
int ACT_BARRIER_MSG =		0;
int ACT_BARRIER_GO =		0;
int ACT_BCASTS =			0;
int ACT_IRECVS =			0;
int ACT_GATHER =			0;
int ACT_GATHER_NUM=			0;
int ACT_WIN_LOCK=			0;
int ACT_WIN_UNLOCK =		0;
int ACT_LOCK_SUCCESS =		0;
int ACT_LOCK_FAILURE =		0;
int ACT_UNLOCK_SUCCESS =	0;
int ACT_UNLOCK_FAILURE =	0;
int ACT_GET_RECV =			0;
int ACT_DUPED =				0;
int ACT_PUT_ATTR_DONE =		0;
int ACT_RET_ATTR =			0;


//Descendent Functions
int DESC_LIST[120];
int DESC_IND;

//thread synch variable
int DONE = 0;
pthread_t pEng;
pthread_mutex_t pEngL; //progress eng lock

int MPI_Init(int *argc, char ***argv)
{
	int new_socket, i, j, rc;
	int ack_len;
	char buf[BUFFER_LEN];

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	
	ack_len = strlen(CLIENT_ACK) + 1;
	
	MSG_Q = g_queue_new();
	WNDS = g_queue_new();

	DEBUG = atoi(getenv("DEBUG"));

	//SET Global MPI_EXEC info -- 
	strcpy(EXEC_SERVER_NAME, getenv("SERVER_NAME"));
	EXEC_PORT = atoi(getenv("PORT"));
	//SET rank and calculate port to listen on
	MPI_RANK= atoi(getenv("MPI_RANK"));
	if(MPI_RANK == 0)
		tsprintf("init begin\n");

	LOCAL_PORT =  MPI_RANK + PORT_CALC;
	
	new_socket = connect_to_server(EXEC_SERVER_NAME, EXEC_PORT );

	//send phonebook request and recieve ack &	
	//Set Global Number of Procceses	
	sprintf(buf, "%s", "phonebook_req");
	send_msg(new_socket, buf, strlen(buf) + 1);
	rc = recv_msg(new_socket, buf);
	NUMPROCS = atoi(buf);
	send_msg(new_socket, CLIENT_ACK, ack_len);

	/*
	//recieve phonebook an entry at a time and ACK each
	PHONEBOOK = (char **)malloc(NUMPROCS * sizeof(char *));
	for (i = 0; i < NUMPROCS; i++)
	{
		recv_msg(new_socket, buf);
		PHONEBOOK[i] = malloc(strlen(buf) + 1);
		sprintf(PHONEBOOK[i], "%s", buf);
		send_msg(new_socket, CLIENT_ACK, ack_len);
	}
	*/

	for(i = 0; i < 150; i++)
	{
		for (j = 0; j < 150; j++)
		{
			PHONEBOOK[i][j] = '\0';
		}
	}

	for (i = 0; i < NUMPROCS; i++)
	{
		recv_msg(new_socket, buf);
		sprintf(PHONEBOOK[i], "%s", buf);
		send_msg(new_socket, CLIENT_ACK, ack_len);
	}

	if (MPI_RANK == 0)
	{
		for (i = 0; i < NUMPROCS; i++)
			tsprintf("%s\n", PHONEBOOK[i]);
	}

	//create the connection table and intialize it
	for(i = 0; i < NUMPROCS; i++)
	{
		RC_TABLE[i] = -1;
		SC_TABLE[i] = -1;
	}

	//set MPI_COMM_WORLDS communicator
	C.size = NUMPROCS;
	C.rank = MPI_RANK;
	calc_tree(MPI_RANK, NUMPROCS,&C.parent,&C.lchild, &C.rchild);

	if (MPI_RANK == 0)
	{
		for(i = 0; i < 500; i++)
		{
			for(j = 0; j < 500; j++)
			{
				ATTR[i][j] = -1;
			}
		}
	}
	
	//setup listening socket
	DEFAULT_SOCKET = setup_to_accept(LOCAL_PORT);

	if(C.rank == 0)
	tsprintf("here 1\n");

	tree_connect();
	
	if(C.rank == 0)
	tsprintf("here 2\n");

	pthread_mutex_init(&pEngL, NULL);
	pthread_create(&pEng, NULL, sub, NULL );
	
	if(C.rank == 0)
	tsprintf("out of init\n");
}

int MPI_Finalize(void)
{
	tsprintf("Finalize\n");
	MPI_Barrier(MPI_COMM_WORLD);
	while ( g_queue_get_length(MSG_Q) > 0 )
	{
		print_msgs();	
	}
	DONE = 1;
	pthread_join(pEng, NULL);

}

int MPI_Abort(MPI_Comm comm, int rc)
{
	int childCount, children[2];
	int new_socket;
	msg hdr;

	childCount = count_children(children);

	handshake(0);

	send_sys_msg(0, NA, comm, ABORT, 0, NULL);
	tsprintf("***ABORT SIGNAL SENT***\n");
}

int MPI_Attr_put(MPI_Comm comm, int keyval, void* value)
{
	int adjComm;
	int num;
	msg toFind, *rp;

	if (comm == MPI_COMM_WORLD)
		adjComm = 0;
	else 
		adjComm = comm;

	num = *(int *)value;

	tsprintf("sending sys message\n");
	handshake(0);
	send_sys_msg(0, num, adjComm, PUT_ATTR, -keyval, NULL );

	toFind_set(&toFind, 0, NA, NA, adjComm, PUT_ATTR_DONE, NA);
	rp = queue_find(&ACT_PUT_ATTR_DONE, &toFind);
	tsprintf("got response\n");
	pthread_mutex_lock(&pEngL);
	g_queue_remove(MSG_Q, rp);
	pthread_mutex_unlock(&pEngL);
}

int MPI_Attr_get(MPI_Comm comm, int keyval, void *attr_value, int *flag)
{
	int adjComm;
	msg toFind, *rp;

	if (comm == MPI_COMM_WORLD)
		adjComm = 0;
	else 
		adjComm = comm;

	handshake(0);
	tsprintf("sending get msg\n");
	send_sys_msg(0, keyval, adjComm, GET_ATTR, NA, NULL);

	toFind_set(&toFind, 0, NA, NA, adjComm, RET_ATTR, NA);
	rp = queue_find(&ACT_RET_ATTR, &toFind);
	tsprintf("getting  get msg back\n");

	*(int *)attr_value = rp->tag;

	pthread_mutex_lock(&pEngL);
	g_queue_remove(MSG_Q, rp);
	pthread_mutex_unlock(&pEngL);
}

int MPI_Gather(void* sendbuf, int sendcnt, MPI_Datatype sendtype, void* recvbuf, int recvcnt, MPI_Datatype recvtype, int root, MPI_Comm comm)
{
	int i, j, rc, dest, pos;
	msg toFind, *rp;
	int  msglen, recvlen;
	int childCount, children[2];
	int recNum;
	int temp;
	char *sndBuf, *intBuf;
	
	childCount = count_children(children);
	
	if (sendtype == MPI_INT)
	{
		intBuf = (char *)malloc(10 * sendcnt + 1);
		intBuf[0] = '\0';
		for (i = 0; i < sendcnt; i++)
		{			
			temp = *(int *)(sendbuf + (i * sizeof(int)));
			sprintf(intBuf,"%s%010d",intBuf, temp);			
		}
		
		msglen = strlen(intBuf);
		sndBuf = intBuf;
	}
	else if (sendtype == MPI_CHAR)
	{
		msglen = strlen(sendbuf);
		sndBuf = sendbuf;
	}

	//send to parent
	if (childCount > 0 && C.rank > 0)
	{
		rc = desc_list(C.rank, C.size);	
		for (i = rc - 1; i >= 0; i--)
		{
			//tsprintf("Finding %d to fwd...\n", DESC_LIST[i]);
			toFind_set(&toFind, NA, NA, DESC_LIST[i], NA, GATHER, NA);			
			//tsprintf("Finding %d to fwd...\n", DESC_LIST[i]);
			rp = queue_find(&ACT_GATHER, &toFind);
			//tsprintf("...Found %d %s\n", rp->tag, rp->msg);

			//forward message to parent
			send_sys_msg(C.parent, rp->tag, rp->comm, GATHER, rp->size, rp->msg);
			
			pthread_mutex_lock(&pEngL);
			g_queue_remove(MSG_Q, rp);
			pthread_mutex_unlock(&pEngL);
		}
	}

	//send own rank
	if (C.rank != 0)
	{
		send_sys_msg(C.parent, C.rank, comm, GATHER, msglen, sndBuf);
	}
	else 
	{
		handshake(0);
		send_sys_msg(0, C.rank, comm, GATHER, msglen, sndBuf);
		//tsprintf("self msg\n");
	}


	//if 0 is not the root send msgs to root
	if (C.rank == 0 && C.rank != root)
	{
		handshake(root);

		for (i = 0; i < C.size; i++)
		{
			toFind_set(&toFind, NA, NA, i, NA, GATHER, NA);
			rp = queue_find(&ACT_GATHER, &toFind);
			//tsprintf("forwarding %d to root %d\n", i, root);
						
			send_sys_msg(root, rp->tag, rp->comm, GATHER, rp->size, rp->msg);

			pthread_mutex_lock(&pEngL);
			g_queue_remove(MSG_Q, rp);
			pthread_mutex_unlock(&pEngL);
		}

	}

	//rank 0 has no parent, msgs itself
	if (C.rank == root)
	{
		//tsprintf("root gathering\n");
		pos = 0;
		for (i = 0; i < C.size; i++)
		{
			toFind_set(&toFind, NA, NA, i, NA, GATHER, NA);
			//tsprintf("gather search for %d\n", i);
			rp = queue_find(&ACT_GATHER, &toFind);
			//tsprintf("found %d\n", i);

			if (recvtype == MPI_INT)
			{
				for (j = 0; j < recvcnt; j++)
				{
					sscanf(rp->msg + j * 10, "%010d", (int *)recvbuf + (i + j));
				}
			}
			else if (recvtype == MPI_CHAR)
			{
				memcpy(recvbuf + pos, rp->msg, rp->size);
			}			
			
			pos += rp->size;

			pthread_mutex_lock(&pEngL);
			g_queue_remove(MSG_Q, rp);
			pthread_mutex_unlock(&pEngL);
		}

		if (recvtype == MPI_CHAR)
			memset(recvbuf + pos, '\0', 1);		
	}

	//tsprintf("before barrier\n");
	MPI_Barrier(comm);
	//tsprintf("after barrier\n");
	free(intBuf);

}

int MPI_Barrier(MPI_Comm comm)
{
	tsprintf("in barrier\n");
	tree_report(comm, BARRIER, &ACT_BARRIER_MSG, BARRIER_GO, &ACT_BARRIER_GO);
	tsprintf("out of barrier\n");
}

int MPI_Bcast(void* buf, int count, MPI_Datatype datatype, int root, MPI_Comm comm)
{
	int i;
	int childCount, children[2];
	int msgLen, temp;
	char *intBuf, *sndBuf;
	msg *rp, toFind;
	GList *listPtr = NULL; 

	childCount = count_children(children);

	if (C.rank == root)
	{
		handshake(0);

		if (datatype == MPI_INT)
		{
			intBuf = (char *)malloc(10 * count + 1);
			intBuf[0] = '\0';
			for (i = 0; i < count; i++)
			{			
				temp = *(int *)(buf + (i * sizeof(int)));
				sprintf(intBuf,"%s%010d",intBuf, temp);			
			}
			msgLen = strlen(intBuf);
			sndBuf = intBuf;
		}
		else if (datatype == MPI_CHAR)
		{
			msgLen = count;
			sndBuf = buf;
		}

		send_sys_msg(0, NA, comm, BCAST, msgLen, sndBuf);
	}

	
	toFind_set(&toFind, NA, NA, NA, NA, BCAST, NA);
	rp = queue_find(&ACT_BCASTS, &toFind);
	

	if (datatype == MPI_INT)
	{
		for (i = 0; i < count; i++)
		{
			sscanf(rp->msg + i * 10, "%010d", (int *)buf + i);
		}
	}
	else if (datatype == MPI_CHAR)
	{
		tsprintf("RECV %s\n", rp->msg);
		memmove(buf, rp->msg, rp->size);
	}

	for (i = 0; i < childCount; i++)
	{
		send_sys_msg(children[i], NA, rp->comm, BCAST, rp->size, rp->msg);
	}
	
	//free(intBuf);
	pthread_mutex_lock(&pEngL);
	g_queue_remove(MSG_Q, rp);
	pthread_mutex_unlock(&pEngL);
}

int MPI_Comm_dup(MPI_Comm comm, MPI_Comm *newComm)
{
	tsprintf("comm dup\n");

	int nextcomm;
	msg toFind, *rp;

	if (C.rank == 0)
		nextcomm = HANDLES_IND++;

	MPI_Bcast(&nextcomm, 1, MPI_INT, 0, comm);

	*newComm = nextcomm;
	tsprintf("%d\n", nextcomm);

	if(C.rank == 0)
	{
		send_sys_msg(0, comm, *newComm, DUP_ATTR, NA, NULL);

		toFind_set(&toFind, 0, NA, NA, NA, DUPED, NA);
		rp = queue_find(&ACT_DUPED, &toFind);
		pthread_mutex_lock(&pEngL);
		g_queue_remove(MSG_Q, rp);
		pthread_mutex_unlock(&pEngL);
	}

	return MPI_SUCCESS;
}

int MPI_Comm_size(MPI_Comm comm, int *size)
{
	*size = C.size;
	return MPI_SUCCESS;

}

int MPI_Comm_rank(MPI_Comm comm, int *rank)
{
	*rank = C.rank;
	return MPI_SUCCESS;
}

int MPI_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status)
{
	int i;
	msg toFind, *rp;
	tsprintf("RECV called for tag %d, source %d\n", tag, source);

	while(RC_TABLE[source] == -1)
	{
		//sleep(1);
	}

	//set msg fields to search in queue for
	toFind_set(&toFind, source, NA, tag, NA, NON_SYS, NA);
	rp = queue_find(NULL, &toFind);
	
	if (datatype == MPI_INT)
	{
		for (i = 0; i < count; i++)
		{
			sscanf(rp->msg + i * 10, "%010d", (int *)buf + i);
		}
	}
	else if (datatype == MPI_CHAR)
	{
		tsprintf("RECV %s\n", rp->msg);
		memmove(buf, rp->msg, rp->size);
	}

	if(status != NULL)
	{
		status->count = count;
		status->cancelled = 0;
		status->MPI_SOURCE = rp->source;
		status->MPI_TAG = rp->tag;
		status->MPI_ERROR = MPI_SUCCESS;
	}

	pthread_mutex_lock(&pEngL);
	g_queue_remove(MSG_Q, rp);
	pthread_mutex_unlock(&pEngL);	

}

int MPI_Isend(void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *handle)
{
	tsprintf("Begin ISEND\n");

	int i, rc, msgLen;
	int socket;
	int temp;
	char *intBuf, *sndBuf;
	msg *msgEnv, *isendEnv;

	if (datatype == MPI_INT)
	{
		intBuf = (char *)malloc(10 * count + 1);
		intBuf[0] = '\0';
		for (i = 0; i < count; i++)
		{			
			temp = *(int *)(buf + (i * sizeof(int)));
			sprintf(intBuf,"%s%010d",intBuf, temp);			
		}
		msgLen = strlen(intBuf);
		sndBuf = intBuf;
	}
	else if (datatype == MPI_CHAR)
	{
		msgLen = count;
		sndBuf = buf;
	}

	handshake(dest);

	pthread_mutex_lock(&pEngL);
	*handle = REQ_INDEX;
	REQ_HANDLES[REQ_INDEX] = 0;
	REQ_INDEX++;
	pthread_mutex_unlock(&pEngL);

	msgEnv = (msg *)malloc(sizeof(msg));
	msgEnv->source = C.rank;
	msgEnv->dest = dest;
	msgEnv->tag = tag;
	msgEnv->comm = comm;
	msgEnv->sys = NON_SYS;
	msgEnv->size = msgLen;
	msgEnv->msg = (char *)malloc(msgLen);
	memcpy(msgEnv->msg, sndBuf, msgLen);

	isendEnv = (msg *)malloc(sizeof(msg));
	isendEnv->source = 0;
	isendEnv->dest = 0;
	isendEnv->tag = *handle;
	isendEnv->comm = 0;
	isendEnv->sys = ISEND;
	isendEnv->msg = (void *)msgEnv;

	pthread_mutex_lock(&pEngL);
	g_queue_push_tail(MSG_Q, isendEnv);
	ACT_ISENDS++;
	tsprintf("Post-inc ACT_ISEND = %d\n", ACT_ISENDS);
	pthread_mutex_unlock(&pEngL);

}	

int MPI_Irecv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *handle)
{
	msg *msgPtr = (msg*)malloc( sizeof(msg) );
	msg *infoPtr= (msg*)malloc( sizeof(msg) );


	//do i need to lock this?
	pthread_mutex_lock(&pEngL);
	*handle = REQ_INDEX;
	REQ_HANDLES[REQ_INDEX] = 0;
	REQ_INDEX++;
	pthread_mutex_unlock(&pEngL);
	

	infoPtr->source = NA;
	infoPtr->dest = NA;
	infoPtr->tag = *handle;
	infoPtr->comm = NA;
	infoPtr->sys = datatype;
	infoPtr->msg = buf;
	infoPtr->size = count;


	msgPtr->source = source;
	msgPtr->dest = NA; //dest has no meaning so it holds the req value;
	msgPtr->tag = tag;
	msgPtr->comm = comm;
	msgPtr->sys = IRECV;
	msgPtr->msg = infoPtr;
	
	pthread_mutex_lock(&pEngL);
	g_queue_push_tail(MSG_Q, msgPtr);
	ACT_IRECVS++;
	pthread_mutex_unlock(&pEngL);


	tsprintf("Irecv hwp complete\n");
}

int MPI_Probe(int source, int tag, MPI_Comm comm, MPI_Status *status)
{
	msg toFind, *rp;

	toFind_set(&toFind,source, NA, tag, NON_SYS, 0, NA);
	rp = queue_find(NULL, &toFind);

	if(status != NULL)
	{
		status->count = rp->size;
		status->cancelled = 0;
		status->MPI_SOURCE = rp->source;
		status->MPI_TAG = rp->tag;
		status->MPI_ERROR = MPI_SUCCESS;
	}

	return (rp != NULL);
}

int MPI_Rsend(void* buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm)
{}

int MPI_Send(void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm)
{
	int i, rc, msgLen, temp;
	int socket, new_socket;
	GList *listPtr = NULL; 
	msg toFind, *rp;
	char *intBuf, *sndBuf;
	
	if (datatype == MPI_INT)
	{
		intBuf = (char *)malloc(10 * count + 1);
		intBuf[0] = '\0';
		for (i = 0; i < count; i++)
		{			
			temp = *(int *)(buf + (i * sizeof(int)));
			sprintf(intBuf,"%s%010d",intBuf, temp);			
		}
		msgLen = strlen(intBuf);
		sndBuf = intBuf;
	}
	else if (datatype == MPI_CHAR)
	{
		msgLen = count;
		sndBuf = buf;
	}


	handshake(dest);
	

	//REQ permission to send
	send_sys_msg(dest, NA, comm, SEND_REQ, 0, NULL);

	toFind_set(&toFind, dest, NA, NA, NA, OK_TO_SEND, NA);
	rp = queue_find(&ACT_SNDREQ_ACKS, &toFind);

	//send msg
	send_sys_msg(dest, tag, comm, NON_SYS, msgLen, sndBuf);

	//free buffer mem and remove send ack from queue
	if(datatype == MPI_INT)
	{
		free(intBuf);
	}
	pthread_mutex_lock(&pEngL);
	g_queue_remove(MSG_Q, rp);
	pthread_mutex_unlock(&pEngL);		
}

int MPI_Ssend(void* buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm)
{
	MPI_Send(buf, count, datatype, dest, tag, comm);
}

int MPI_Test(MPI_Request *handle, int *flag, MPI_Status *status)
{
	*flag = 0;

	if (REQ_HANDLES[*handle] != -1)
	{
		*flag = 1;
	}

}

int MPI_Wait(MPI_Request *handle, MPI_Status *status)
{
	tsprintf("MPI_wait on %d\n", *handle);
	while(!REQ_HANDLES[*handle])
	{}
}

int MPI_Get(void *origin_addr, int origin_count, MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Win win)
{
	int i, rc;
	char sndBuf[50];
	msg toFind, *rp;

	tsprintf("Get\n");
			
	sprintf(sndBuf, "%010d%010d%010d%010d%010d",target_rank, target_disp, target_count, target_datatype, win);
	send_sys_msg(target_rank, NA, NA, GET_SEND, 50, sndBuf);

	tsprintf("Finding GET_RECV for %d...\n", target_rank);
	toFind_set(&toFind, target_rank, NA, NA, NA, GET_RECV, NA);
	rp = queue_find(&ACT_GET_RECV, &toFind);
	tsprintf("Found GET_RECV for %d...\n", target_rank);

	if (origin_datatype == MPI_INT)
	{
		for (i = 0; i < origin_count; i++)
		{
			sscanf(rp->msg + i * 10, "%010d", (int *)origin_addr + i);
		}

		/*
		for (i = 0; i < origin_count; i++)
		{
			tsprintf("%d ", *(int *)origin_addr + i);
		}
		printf("\n");
		*/

	}
	else if (origin_datatype == MPI_CHAR)
	{
		tsprintf("RECV %s\n", rp->msg);
		memmove(origin_addr, rp->msg, rp->size);
	}


	pthread_mutex_lock(&pEngL);
	g_queue_remove(MSG_Q, rp);
	pthread_mutex_unlock(&pEngL);
}

int MPI_Put(void *origin_addr, int origin_count, MPI_Datatype origin_datatype, int target_rank, MPI_Aint target_disp, int target_count, MPI_Datatype target_datatype, MPI_Win win)
{
	int i, rc, temp, msgLen;
	char intBuf[10 * origin_count], *msgBuf, sndBuf[1024], putBuf[50];

	tsprintf("Put\n");

	if (origin_datatype == MPI_INT)
	{
		intBuf[0] = '\0';
		for (i = 0; i < origin_count; i++)
		{			
			temp = *(int *)(origin_addr + (i * sizeof(int)));
			sprintf(intBuf,"%s%010d",intBuf, temp);			
		}
		msgLen = strlen(intBuf);
		msgBuf = intBuf;
	}
	else if (origin_datatype == MPI_CHAR)
	{
		msgLen = origin_count;
		msgBuf = origin_addr;
	}

	handshake(target_rank);


	sprintf(putBuf, "%010d%010d%010d%010d%010d",target_rank, target_disp, target_count, target_datatype, win);
	
	//sndBuf = (char *)malloc(msgLen + 50);
	sprintf(sndBuf, "%s%s", putBuf, msgBuf);
	
	send_sys_msg(target_rank, NA, NA, PUT, msgLen + 50, sndBuf);
	//free(sndBuf);
}

int MPI_Win_create(void *base, MPI_Aint size, int disp_unit, MPI_Info info , MPI_Comm comm, MPI_Win *win)
{
	wnd *newWin;
	
	newWin = (wnd *)malloc(sizeof(wnd));
	newWin->handle = *win = WIN_INDEX++;
	newWin->base = base;
	newWin->lock = -1; //initialize to unlocked
	newWin->locked_by = -1;//intialize to shared lock
	newWin->size = size;
	newWin->disp_unit = disp_unit;
	newWin->info = info;
	newWin->comm = comm;
	newWin->wait_q = g_queue_new();
			
	pthread_mutex_lock(&pEngL);
	g_queue_push_tail(WNDS, newWin);
	pthread_mutex_unlock(&pEngL);

	tsprintf("win created\n");
}

int MPI_Win_lock(int lock_type , int rank, int assert, MPI_Win win)
{
	int done = 0;
	msg toFind, *rp;

	handshake(rank);
	send_sys_msg(rank, lock_type, win, WIN_LOCK, 0, NULL);
	while(!done)
	{
		if (ACT_LOCK_SUCCESS > 0)
		{
			toFind_set(&toFind, rank, NA, NA, win, LOCK_SUCCESS, NA);
			if( rp = asynch_queue_find(&ACT_LOCK_SUCCESS, &toFind) )
			{
				tsprintf("win lock success\n");
				done = 1;
				pthread_mutex_lock(&pEngL);
				g_queue_remove(MSG_Q, rp);
				pthread_mutex_unlock(&pEngL);
			}
		}
		else if(ACT_LOCK_FAILURE > 0)
		{
			toFind_set(&toFind, rank, NA, NA, win, LOCK_FAILURE, NA);
			if( rp = asynch_queue_find(&ACT_LOCK_FAILURE, &toFind) )
			{
				send_sys_msg(rank, lock_type, win, WIN_LOCK, 0, NULL);
				pthread_mutex_lock(&pEngL);
				g_queue_remove(MSG_Q, rp);
				pthread_mutex_unlock(&pEngL);
			}
		}
	}
	
}

int MPI_Win_unlock(int rank, MPI_Win win)
{
	int done = 0;
	msg toFind, *rp;

	tsprintf("win unlock\n");

	handshake(rank);
	send_sys_msg(rank, NA, win, WIN_UNLOCK, 0, NULL);
	tsprintf("unlock msg sent\n");
	while(!done)
	{
		if (ACT_UNLOCK_SUCCESS > 0)
		{
			toFind_set(&toFind, rank, NA, NA, win, UNLOCK_SUCCESS, NA);
			if( rp = asynch_queue_find(&ACT_UNLOCK_SUCCESS, &toFind) )
			{
				tsprintf("Unlock Success\n");
				done = 1;
				pthread_mutex_lock(&pEngL);
				g_queue_remove(MSG_Q, rp);
				pthread_mutex_unlock(&pEngL);
			}
		}
		else if(ACT_LOCK_FAILURE > 0)
		{
			toFind_set(&toFind, rank, NA, NA, win, UNLOCK_FAILURE, NA);
			if( rp = asynch_queue_find(&ACT_UNLOCK_FAILURE, &toFind) )
			{
				send_sys_msg(rank, NA, win, WIN_UNLOCK, 0, NULL);
				pthread_mutex_lock(&pEngL);
				g_queue_remove(MSG_Q, rp);
				pthread_mutex_unlock(&pEngL);
			}
		}
	}

}

double MPI_Wtime()
{
	struct timeval tv;

	gettimeofday(&tv, (struct timezone *) 0 );

	return ( tv.tv_sec + (tv.tv_usec / 1000000.0) );
}

double MPI_Wtick()
{
	struct timespec res;
	int rc;

	rc = clock_getres( CLOCK_REALTIME, &res );
	if (!rc) 
		return res.tv_sec + 1.0e-6 * res.tv_nsec;
}

//progress engine
void *sub(void *arg)
{
	int i, j, rc; 
	int tcnum; //delete me
	int childCount, children[2];
	int new_socket;
	fd_set read_fds; //used for new connections & incoming messages
	struct timeval tv;
	msg *env, *hsEnv, *msgPtr, *isendPtr, *irecvPtr, *infoPtr;
	msg toFind, *rp;
	GList *listPtr = NULL;

	//tsprintf("Progress Engine Initiated\n");

	tcnum = 0;
	while(!DONE)
	{	

		//accept new connections
		set_fds(&read_fds, &DEFAULT_SOCKET, 1, &tv);
		rc = select(FD_SETSIZE, &read_fds, NULL, NULL, &tv);
		if (FD_ISSET(DEFAULT_SOCKET, &read_fds) == 1)
		{
			hsEnv = (msg *)malloc( sizeof(msg) );

			new_socket = accept_connection(DEFAULT_SOCKET);

			read_env(new_socket, hsEnv);
			//tsprintf("read handshake header\n");
			if (hsEnv->sys == HANDSHAKE)
			{
				if(hsEnv->source == C.rank)
				{
					RC_TABLE[hsEnv->source] = new_socket;
				}
				else
				{
					SC_TABLE[hsEnv->source] = RC_TABLE[hsEnv->source] = new_socket;
					send_sys_msg(hsEnv->source, 0, 0, RECV_OK, 0, NULL);
				}

				//tsprintf("proge eng Handshake with %d\n", hsEnv->source);
			}
			else
			{
				tsprintf("Error - Non HS msg recv during HS\n");
				tsprintf("hdr->sys = %d\n", hsEnv->sys);
			}
			free(hsEnv);
		}


		//if (C.rank == 0)
					//tsprintf("reading incoming\n");
		//read incoming messages
		set_C_TABLE_fds(&read_fds, &tv);
		rc = select(FD_SETSIZE, &read_fds, NULL, NULL, &tv);
		for(i=0; i < NUMPROCS; i++)
		{

			if ( (RC_TABLE[i] != -1 && (FD_ISSET(RC_TABLE[i], &read_fds) == 1)))
			{
				//if (C.rank == 0)
					//tsprintf("incoming msg\n");
				env = (msg *)malloc( sizeof(msg) );
				rc = read_env(RC_TABLE[i], env);
				//if (C.rank == 0)
					//tsprintf("incoming msg read\n");
				//don't close a connection if you still havea message from it
				if (rc == -1)
				{
					//tsprintf("closing connection\n");
					toFind_set(&toFind, i, NA, NA, NA, NA, NA);
					if (asynch_queue_find(NULL, &toFind) == NULL)
					{
						RC_TABLE[i] = -1;
						tsprintf("closing %d\n", i);

					}
					free(env);
				}
				else
				{	
					//if(C.rank == 0)
						//tsprintf("processing msg\n");
					if(rc = process_msg(env, RC_TABLE[i]))
					{
						//tsprintf("reading message\n");
						read_msg(RC_TABLE[i], env);
						pthread_mutex_lock(&pEngL);
						g_queue_push_tail(MSG_Q, env);
						pthread_mutex_unlock(&pEngL);
						//if (C.rank == 0)
							//tsprintf("done processing msg\n");
					}
				}
			}
		}
		//if (C.rank == 0)
			//tsprintf("done reading incoming\n");



		if (ACT_ISENDS > 0) //no need to lock because HWP doesn't dec this value
		{
			toFind_set(&toFind, NA, NA, NA, NA, ISEND, NA);
			tsprintf("looking for isend env\n");
			isendPtr = queue_find(NULL, &toFind);
			tsprintf("found isend env\n");
			msgPtr = isendPtr->msg;

			send_sys_msg(msgPtr->dest, msgPtr->tag, msgPtr->comm, NON_SYS, msgPtr->size, msgPtr->msg);

			pthread_mutex_lock(&pEngL);
			REQ_HANDLES[isendPtr->tag] = 1;
			ACT_ISENDS--;
			pthread_mutex_unlock(&pEngL);

			tsprintf("Isend complete for tag %d\n", msgPtr->tag);
			g_queue_remove(MSG_Q, isendPtr);
		}

		if(ACT_IRECVS > 0)
		{
			//get the msg header that describes the message to irecv
			//and its .msg field will point to the another struct
			//that contains the datatype, count, and a buffer pointer
			toFind_set(&toFind, NA, NA, NA, NA, IRECV, NA);
			irecvPtr = queue_find(NULL, &toFind);
			infoPtr = irecvPtr->msg;

			//get the actual message to irecv
			toFind_set(&toFind, irecvPtr->source, NA, irecvPtr->tag, irecvPtr->comm, NON_SYS, NA);
			rp = asynch_queue_find(NULL, &toFind);

			if (rp)
			{
				if(infoPtr->sys == MPI_INT)
				{
					for (i = 0; i < infoPtr->size; i++)
					{
						sscanf(rp->msg + i * 10, "%010d", (int *)infoPtr->msg + i);
					}
				}
				else if(infoPtr->sys == MPI_CHAR)
				{
					memcpy(infoPtr->msg, rp->msg, infoPtr->size);
				}

				pthread_mutex_lock(&pEngL);
				REQ_HANDLES[infoPtr->tag] = 1;
				pthread_mutex_unlock(&pEngL);
	
				pthread_mutex_lock(&pEngL);
				free(infoPtr);
				g_queue_remove(MSG_Q, irecvPtr);
				g_queue_remove(MSG_Q, rp);
				pthread_mutex_unlock(&pEngL);		
			
				ACT_IRECVS--;
			}

		}

		
		if (C.rank == 0 && tcnum % 100000 == 0)
		{
			tsprintf("going around\n\n");
			
		}
		tcnum++;
		
	}
}
int process_msg(msg *env, int fd)
{
	int queueMsg = 1; //boolean return - should msg be queued
	int i, j, rc, temp, msgLen, *queued;
	int childCount, children[2];
	int target_rank, target_disp, target_count, target_datatype, win;
	char putMsg[50], *putRecMsg, *intBuf, *msgBuf;


	if(env->sys == ABORT)
	{
		childCount = count_children(children);

		for(j=0; j < childCount; j++)
		{
			send_sys_msg(children[j], NA, env->comm, ABORT, 0, NULL);
		}
		tsprintf("Abort Signal Recieved - Exiting\n");
		exit(-1);
	}
	else if(env->sys == WIN_LOCK)
	{
		//all lock calls are treated as MPI_LOCK_EXCLUSIVE
		wnd *rp;
		rp = wnd_find(env->comm);

		if(rp != NULL)
		{
			if (rp->lock == -1)
			{
				rp->lock = env->source;
				send_sys_msg(env->source, NA, env->comm, LOCK_SUCCESS, 0, NULL);
			}
			else
			{
				int *toQ = (int *)malloc(sizeof(int));
				*toQ = env->source;
				tsprintf("queueing %d\n", *toQ);
				pthread_mutex_lock(&pEngL);
				g_queue_push_tail(rp->wait_q, toQ);
				pthread_mutex_unlock(&pEngL);
			}
		}
		queueMsg = 0;
	}
	else if(env->sys == WIN_UNLOCK)
	{
		wnd *rp;

		tsprintf("thread win unlock\n");

		rp = wnd_find(env->comm);
		if(rp != NULL)
		{
			if (rp->lock == env->source)
			{
				tsprintf("unlock of %d successful for %d\n", env->comm, env-> source);
				send_sys_msg(env->source, NA, env->comm, UNLOCK_SUCCESS, 0, NULL);
				//schedule anyone in the queue or if its empty just unlock it
				if( g_queue_get_length(rp->wait_q) > 0 )
				{
					queued = (int *)g_queue_pop_head(rp->wait_q);
					rp->lock = *queued;
					send_sys_msg(*queued, NA, env->comm, LOCK_SUCCESS, 0, NULL);

				}
				else
				{
					rp->lock = -1;
				}

			}
			else
			{
				tsprintf("unlock failure\n");
				send_sys_msg(env->source, NA, env->comm, UNLOCK_FAILURE, 0, NULL);
			}
		}

		queueMsg = 0;
	}
	else if (env->sys == PUT)
	{
		tsprintf("thread put\n");
		wnd *rp;		
		void *adjBase;

		putRecMsg = (char *)malloc(env->size);

		recv_msg_by_size(fd, putMsg, 50);
		recv_msg_by_size(fd, putRecMsg, env->size - 50);
		sscanf(putMsg, "%010d%010d%010d%010d%010d", &target_rank, &target_disp, &target_count, &target_datatype, &win);
		//tsprintf("tp tr:%d, td:%d, tc:%d, tdat:%d, w:%d\n", target_rank, target_disp, target_count, target_datatype, win);

		rp = wnd_find(win);

		tsprintf("base = %p, disp = %d, adjBase = %p\n", rp->base, target_disp, adjBase);
		 
		if (target_datatype == MPI_INT)
		{
			adjBase = rp->base + target_disp * sizeof(int);
			for (i = 0; i < target_count; i++)
			{
				sscanf(putRecMsg + i * 10, "%010d", (int *)adjBase + i);
			}

		}
		else if (target_datatype == MPI_CHAR)
		{
			adjBase = rp->base + target_disp;
			memmove(adjBase, putRecMsg, target_count);
		}

		free(putRecMsg);
		tsprintf("out of thread put\n");
		queueMsg = 0;
	}
	else if(env->sys == GET_SEND)
	{
		tsprintf("thread get\n");
		wnd *rp;
		recv_msg_by_size(fd, putMsg, 50);
		sscanf(putMsg, "%010d%010d%010d%010d%010d", &target_rank, &target_disp, &target_count, &target_datatype, &win);
		
		tsprintf("tg finding window %d...\n", win);
		rp = wnd_find(win);
		tsprintf("...tg found window %d\n", win);

		if (target_datatype == MPI_INT)
		{
			intBuf = (char *)malloc(10 * target_count + 1);
			intBuf[0] = '\0';
			for (i = 0; i < target_count; i++)
			{			
				temp = *(int *)(rp->base + (i * sizeof(int)));
				sprintf(intBuf,"%s%010d",intBuf, temp);			
			}
			msgLen = strlen(intBuf);
			msgBuf = intBuf;
		}
		else if (target_datatype == MPI_CHAR)
		{
			msgLen = target_count;
			msgBuf = rp->base;
		}

		send_sys_msg(env->source, NA, NA, GET_RECV, msgLen, msgBuf);

		free(intBuf);
		queueMsg = 0;
	}
	else if(env->sys == DUP_ATTR)
	{
		int comm = env->tag;
		int newcomm = env->comm;
		if (comm== MPI_COMM_WORLD)
		{
			comm = 0;
		}
		else if (newcomm == MPI_COMM_WORLD)
		{
			newcomm = 0;
		}

		tsprintf("newc = %d, comm = %d\n", newcomm, comm);
		for (i = 0; i < 500; i++)
		{
			ATTR[newcomm][i] = ATTR[comm][i];
		}

		send_sys_msg(env->source, NA, NA, DUPED, NA, NULL);

		queueMsg = 0;
	}
	else if(env->sys == PUT_ATTR)
	{
		tsprintf("ATTR[%d][%d] = %d\n",env->comm,-(env->size),env->tag);
		ATTR[env->comm][-(env->size)] = env->tag;

		send_sys_msg(env->source, NA, env->comm, PUT_ATTR_DONE, NA, NULL);

		queueMsg = 0;
	}
	else if(env->sys == GET_ATTR)
	{
		send_sys_msg(env->source, ATTR[env->comm][env->tag], env->comm, RET_ATTR, NA, NULL);		
		queueMsg = 0;
	}
	//tsprintf("incoming message from rank %d\n", i);
	//if it is a request to send, respond to the request
	else if(env->sys == SEND_REQ)
	{
		//tsprintf("SEND_REQ Recieved\n");
		send_sys_msg(env->source, NA, env->comm, OK_TO_SEND, 0, NULL);
		queueMsg = 0;
	}
	else if(env->sys == OK_TO_SEND)
	{
		//tsprintf("OK_TO_SEND recieved\n");
		ACT_SNDREQ_ACKS++;
		//tsprintf("OK_TO_SENDS = %d\n", ACT_SNDREQ_ACKS);
	}
	else if(env->sys == BARRIER)
	{
		ACT_BARRIER_MSG++;
	}
	else if(env->sys == BARRIER_GO)
	{
		ACT_BARRIER_GO++;
		//tsprintf("BARRIER_GO recieved\n");
	}
	else if(env->sys == BCAST)
	{
		ACT_BCASTS++;
	}
	else if(env->sys == GATHER)
	{
		//tsprintf("incoming gather\n");
		ACT_GATHER++;
	}
	else if(env->sys == LOCK_SUCCESS)
	{
		ACT_LOCK_SUCCESS++;
	}
	else if(env->sys == LOCK_FAILURE)
	{
		ACT_LOCK_FAILURE++;
	}
	else if(env->sys == UNLOCK_SUCCESS)
	{
		ACT_UNLOCK_SUCCESS++;
	}
	else if(env->sys == UNLOCK_FAILURE)
	{
		ACT_UNLOCK_FAILURE++;
	}
	else if(env->sys == GET_RECV)
	{
		ACT_GET_RECV++;
	}
	else if(env->sys == DUPED)
	{
		ACT_DUPED++;
	}
	else if(env->sys == PUT_ATTR_DONE)
	{
		ACT_PUT_ATTR_DONE++;
	}
	else if(env->sys == RET_ATTR)
	{
		ACT_RET_ATTR++;
	}
	return queueMsg;	
}


//treefunctions
calc_tree(int rank, int size, int *parent, int *lchild, int *rchild)
{
	int lchildEval = (2 * rank + 1);
	int rchildEval = (2 * rank + 2);
	
	if (rank == 0)
		*parent = -1;
	else 
		*parent = (rank-1) / 2;
	
	if (lchildEval > size - 1)
		*lchild = -1;
	else
		*lchild = lchildEval;
	
	if (rchildEval > size- 1)
		*rchild = -1;
	else
		*rchild = rchildEval;
}
tree_connect()
{
		int i, childCount = 0, fds[2];

		childCount = 0;
		if(C.lchild != -1)
		{
			fds[childCount] = C.lchild;
			childCount++;
		}
		if(C.rchild != -1)
		{
			fds[childCount] = C.rchild;
			childCount++;
		}
		

		//wait for parent connection
		if (C.rank != 0)
		{
			SC_TABLE[C.parent] = RC_TABLE[C.parent] = accept_connection(DEFAULT_SOCKET);
		}

		if (childCount > 0)
		{
			for(i = 0; i < childCount; i++)
			{
				SC_TABLE[fds[i]] = RC_TABLE[fds[i]] = connect_to_server(PHONEBOOK[fds[i]], fds[i] + PORT_CALC);
			}
		}
}
int count_children(int children[])
{	
	int childCount = 0;

	if(C.lchild != -1)
	{
		children[childCount] = C.lchild;
		childCount++;
	}
	if(C.rchild != -1)
	{
		children[childCount] = C.rchild;
		childCount++;
	}

	return childCount;
}
//[in] repVar -		status to report
//[in] actRepVar -	flag that indicates the message matching that status has been recieved by the root
//[in] goVar -		command from rank to continue
//[in] actGoVar -	flag indicating the command to go has arrived
int tree_report(MPI_Comm comm, int repVar, int *actRepVar, int goVar, int *actGoVar)
{
	int i, j, childCount, num, sum = 0;
	int children[2];
	msg *msgPtr;
	msg toFind, *rp;
		
	childCount = count_children(children);

	if (childCount == 0 && C.rank != 0)
	{
		num = 1;
		//tsprintf("TR leaf node sending %d to %d\n", num, C.parent);
		send_sys_msg(C.parent, num, comm, repVar, 0, NULL);
	}
	else
	{
		i = 0;
		while (i < childCount)
		{
			//tsprintf("seraching for barrier inc var from %d\n", children[i]);
			//print_msgs();
			//for (j = 0; j < C.size; j++)
			//{
			//	tsprintf("SC_TABLE[%d] = %d && RC_TABLE[%d] = %d\n", j, SC_TABLE[j], j, RC_TABLE[j]);
			//}

			toFind_set(&toFind, children[i], NA, NA, NA, repVar, NA);
			rp = queue_find(actRepVar, &toFind);
			//tsprintf("got bar in  var from %d\n", children[i]);
			
			//get msg
			sum += rp->tag;

			pthread_mutex_lock(&pEngL);
			g_queue_remove(MSG_Q, rp);
			pthread_mutex_unlock(&pEngL);		
			i++;
		}

		if (C.rank != 0)
		{
			sum++;
			//tsprintf("Sending bar inc var = %d to %d\n", sum, C.parent);
			send_sys_msg(C.parent, sum, comm, repVar, 0, NULL);			
		}
	}

	if (C.rank == 0)
	{
		//tsprintf("starting Go message\n");
		for (i=0; i < childCount; i++)
		{
			send_sys_msg(children[i], NA, comm, goVar, 0, NULL);
		}
	}
	else
	{
			//tsprintf("waiting for go message\n");
			toFind_set(&toFind, NA, NA, NA, NA, goVar, NA);
			rp = queue_find(actGoVar, &toFind);
			
			pthread_mutex_lock(&pEngL);
			g_queue_remove(MSG_Q, rp);
			pthread_mutex_unlock(&pEngL);		

			//tsprintf("GO_MESSAGE Recieved\n");

			for (i=0; i < childCount; i++)
			{
				//tsprintf("sending go message to %d\n", children[i]);
				send_sys_msg(children[i], NA, comm, goVar, 0, NULL);
			}
	}

	//tsprintf("barrier out\n");
}
int quickSort(int *arr, int elements) 
{
  #define  MAX_LEVELS  1000

  int  piv, beg[MAX_LEVELS], end[MAX_LEVELS], i=0, L, R ;

  beg[0]=0; end[0]=elements;
  while (i>=0) 
  {
    L=beg[i]; R=end[i]-1;
    if (L<R) 
	{
      piv=arr[L]; if (i==MAX_LEVELS-1) return 0;
      while (L<R) 
	  {
        while (arr[R]>=piv && L<R) R--; if (L<R) arr[L++]=arr[R];
        while (arr[L]<=piv && L<R) L++; if (L<R) arr[R--]=arr[L];
	  }
      arr[L]=piv; beg[i+1]=L+1; end[i+1]=end[i]; end[i++]=L;
	}
    else 
	{
      i--; 
	}
  }
  return 1; 
}
//uses the GLOBALS DESC_LIST & DESC_IND
// to return values
decs(int rank, int size)
{
	int lchild, rchild, *toIns;
	
	lchild = rank * 2 + 1;
	rchild = lchild + 1;

	if (lchild <= size - 1)
	{
		DESC_LIST[DESC_IND++] = lchild;
		decs(lchild, size);
	}

	if (rchild <= size - 1)
	{
		DESC_LIST[DESC_IND++] = rchild;
		decs(rchild, size);
	}
}
int desc_list(int rank, int size)
{
	DESC_IND = 0;
	decs(rank, size);

	if(DESC_IND > 1)
		quickSort(DESC_LIST, DESC_IND);		
	return DESC_IND;
}


//messaging functions
read_msg(int fd, msg *env)
{
	int rc;
	
	if (env->size > 0)
	{
		env->msg = malloc(env->size);
		rc = recv_msg_by_size(fd, env->msg, env->size);
	}

}
int read_env(int fd, msg *env)
{
	char buf[10 * 6];
	int size, rc;

	env->msg = NULL;

	rc = recv_msg_by_size(fd, buf, 10 * 6);

	//tsprintf("read header rc = %d\n", rc);

	sscanf(buf, "%010d%010d%010d%010d%010d%010d", &env->source, &env->dest,
									   &env->tag, &env->comm, 
									   &env->sys, &env->size);

	if (rc != -1)
	{
		tsprintf("s:%d d:%d t:%d c:%d sys:%d sz:%d\n", env->source, env-> dest, env->tag, env->comm, env->sys, env->size);
	}

	return rc;
}
send_handshake_id(int dest, int fd)
{
	int i;
	char msgTag[60];

	sprintf(msgTag, "%010d%010d%010d%010d%010d%010d", C.rank, dest, 0,
												0, HANDSHAKE, 0);

	send_msg(fd, msgTag, 6 * 10);	
}
send_sys_msg(int dest, int tag, int comm, int sys, int size, const char *buf)
{
	int i, msglen, rc;
	char env[61], *message;
	int adjSize;
	msg *toSelf;

	//tsprintf("send sys mesg\n");

	sprintf(env, "%010d%010d%010d%010d%010d%010d", C.rank, dest, tag,
												comm, sys, size);
	if(size < 0)
		adjSize = 0;
	else
		adjSize = size;

	msglen = 60 + adjSize;
	message = (char *)malloc(msglen);
	memcpy(message, env, 60);
	memcpy(message + 60, buf, adjSize);


	if(SC_TABLE[dest] == -1)
	{
		tsprintf("********ERROR - Bad FD for %d\n", dest);
	}
	send_msg(SC_TABLE[dest], message, msglen);	

	free(message);
}
send_msg(int fd, void *buf, int size)	
{
    int n;
	char msg[30];
	
    n = write(fd, buf, size);

	sprintf(msg, "send_msg write to fd%d", fd);
    error_check(n, msg);
}
int recv_msg(int fd, char *buf)
{
    int bytes_read;

    bytes_read = read(fd, buf, BUFFER_LEN);
    error_check( bytes_read, "recv_msg read");
    if (bytes_read == 0)
	return(RECV_EOF);
    return( bytes_read );
}
int recv_msg_by_size(int fd, void *buf, int size)
{
    int bytes_read = 0;
	int adjPtr = 0;

	while(bytes_read < size)
	{
		bytes_read += read(fd, buf + bytes_read, size);

		error_check( bytes_read, "recv_msg read");
		if (bytes_read == 0)
			return(RECV_EOF);
	}

	return( bytes_read );
}
error_check(int val, char *str)	
{
    if (val < 0)
    {
		tsprintf("%s :%d: %s\n", str, val, strerror(errno));
		exit(1);
    }
}
handshake(int rank)
{
	msg env;
	int new_socket;
	
	if (SC_TABLE[rank] == -1)
	{
			//tsprintf("handshaking\n");
			new_socket = connect_to_server(PHONEBOOK[rank], rank + PORT_CALC);
			send_handshake_id(rank, new_socket);
			if (rank == C.rank)
			{
				SC_TABLE[rank] = new_socket;
				//tsprintf("shake self\n");
				while (RC_TABLE[rank] == -1)
				{}
			}
			else
			{
				read_env(new_socket, &env);
				RC_TABLE[rank] = SC_TABLE[rank] = new_socket;
				//tsprintf("read the hs response\n");
			}
			//tsprintf("handshake with %d complete\n", rank);
	}
}


//file descriptor setting functions
set_fds(fd_set *set, int *fds, int fdsNum, struct timeval *tv)
{
	int i;

	FD_ZERO(set);
	for (i = 0; i < fdsNum; i++)
	{
		FD_SET(fds[i], set);
	}
	tv->tv_sec = 0;
	tv->tv_usec = 0;
}
set_C_TABLE_fds(fd_set *set, struct timeval *tv)
{
	int i;
	FD_ZERO(set);

	for (i = 0; i < NUMPROCS; i++)
	{
		if (RC_TABLE[i] != -1)
		{
			FD_SET(RC_TABLE[i], set);
			//tsprintf("Setting %d\n", i);
		}
	}


	tv->tv_sec = 0;
	tv->tv_usec = 0;
}


//queue search functions
gint node_comp(gconstpointer a, gconstpointer b)
{
	int rc;
	const msg *ptrA = a;
	const msg *ptrB = b;

	if(ptrA->sys == NON_SYS && ptrB->sys != NA)
	{
		rc = 1;

		if(ptrB->source == MPI_ANY_SOURCE && ptrB->tag == MPI_ANY_TAG)
		{
			rc =  0;
		}
		else if (ptrB->source == MPI_ANY_SOURCE)
		{
			if(ptrB->tag == ptrA->tag)
				rc = 0;		
		}
		else if (ptrB->tag == MPI_ANY_TAG)
		{
			if(ptrB->source == ptrA->source)
				rc = 0;
		}
		else if (ptrB->source == ptrA->source && ptrB->tag == ptrA->tag)
		{
				rc = 0;
		}	
	}
	else
	{
		rc = 0;

		if(ptrB->source != NA && ptrB->source != ptrA->source)
		{
			//tsprintf("source wrong\n");
			rc++;
		}
		if(ptrB->dest != NA && ptrB->dest != ptrA->dest)
		{
			//tsprintf("dest wrong\n");
			rc++;
		}
		if(ptrB->tag != NA && ptrB->tag != ptrA->tag)
		{			
			//tsprintf("tag wrong\n");
			rc++;
		}
		if(ptrB->comm != NA && ptrB->comm != ptrA->comm)
		{
			//tsprintf("comm wrong\n");
			rc++;
		}
		if(ptrB->sys != NA && ptrB->sys != ptrA->sys)
		{
			//tsprintf("sys wrong\n");
			rc++;
		}
		if(ptrB->size != NA && ptrB->size != ptrA->size)
		{
			rc++;
		}
	}
	return rc;
}
gint wnd_comp(gconstpointer a, gconstpointer b)
{
	int rc = 1;
	const wnd *ptrA = a;
	const wnd *ptrB = b;

	if(ptrA->handle == ptrB->handle)
	{
		rc = 0;
	}

	return rc;
}
//IN - GLOBAL - the shared global variable that communicates with the
//Progress Engine Thread.  Otherwise it is NULL.
//IN - msgPtr - is a copy of the msg node to search for in the queue
//OUT - rPtr - is a pointer to the matching node in the queue
msg *queue_find(int *GLOBAL, msg *msgPtr)
{
	GList *listPtr = NULL; 
	int dummyGlobal = 1;

	//if there is no global value to check
	//just use the dummy var to cause the loop to execute
	if(!GLOBAL)
		GLOBAL = &dummyGlobal; 

	while(!listPtr)
	{
		if (*GLOBAL > 0)
		{
			pthread_mutex_lock(&pEngL);
			if( g_queue_get_length(MSG_Q) > 0 )
			{
				//if (C.rank == 0)
				//tsprintf("finding %d %d\n", msgPtr->source, msgPtr->sys);
				listPtr = g_queue_find_custom(MSG_Q, msgPtr,(GCompareFunc)node_comp);
			}
			pthread_mutex_unlock(&pEngL);
		}
	}
	*GLOBAL--;

	return (msg *)listPtr->data; //convert pointer to a list to a pointer to a msg struct
}
msg *asynch_queue_find(int *GLOBAL, msg *msgPtr)
{
	GList *listPtr = NULL; 
	int dummyGlobal = 1;
	msg *rp = NULL;

	//if there is no global value to check
	//just use the dummy var to cause the loop to execute
	if(!GLOBAL)
		GLOBAL = &dummyGlobal; 

	if(!listPtr)
	{
		if (*GLOBAL > 0)
		{
			pthread_mutex_lock(&pEngL);
			if( g_queue_get_length(MSG_Q) > 0 )
			{
				listPtr = g_queue_find_custom(MSG_Q, msgPtr,(GCompareFunc)node_comp);
			}
			pthread_mutex_unlock(&pEngL);
		}
	}


	if(listPtr)
	{
		*GLOBAL--;
		rp = listPtr->data;
	}

	//tsprintf("aqf ptr = %p\n", rp);

	return rp; //convert pointer to a list to a pointer to a msg struct
}
wnd *wnd_find(MPI_Win win)
{
	GList *listPtr = NULL;
	wnd toFind;

	toFind.handle = win;

	pthread_mutex_lock(&pEngL);
	listPtr = g_queue_find_custom(WNDS, &toFind,(GCompareFunc)wnd_comp);
	pthread_mutex_unlock(&pEngL);


	return (wnd *)listPtr->data; //convert pointer to a list to a pointer to a wnd struct
}
toFind_set(msg *toFind, int source, int dest, int tag, int comm, int sys, int size)
{
	bzero( (void *)toFind, sizeof(msg) );

	if (source == NA)
		toFind->source = NA;
	else
		toFind->source = source;

	if (dest == NA)
		toFind->dest = NA;
	else
		toFind->dest = dest;

	if (tag == NA)
		toFind->tag = NA;
	else
		toFind->tag = tag;

	if (comm == NA)
		toFind->comm = NA;
	else
		toFind->comm = comm;

	if (sys == NA)
		toFind->sys = NA;
	else
		toFind->sys = sys;

	if (size == NA)
		toFind->size = NA;
	else
		toFind->size = size;
}


//debug functions
print_msgs()
{
	tsprintf("*******************************\n");
	tsprintf("Queue size = %d\n", g_queue_get_length(MSG_Q));
	g_queue_foreach(MSG_Q, (GFunc)print_msg_data, NULL);
	tsprintf("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n\n");
}
GFunc print_msg_data(gpointer data, gpointer user_data)
{
	msg * ptr = data;

	tsprintf("ptr->source = %d\n",(msg *)ptr->source);
	tsprintf("ptr->dest = %d\n", (msg *)ptr->dest);
	tsprintf("ptr->tag = %d\n", (msg *)ptr->tag);
	//tsprintf("ptr->comm = %d\n", (msg *)ptr->comm);
	tsprintf("ptr->sys = %d\n", (msg *)ptr->sys);
	//tsprintf("ptr->size = %d\n",(msg *)ptr->size);
	tsprintf("ptr->msg = %s\n", (msg *)ptr->msg);
	tsprintf("\n");
}
int tsprintf(const char* format, ...)
{
	if (DEBUG)
	{
		va_list args;
		char buf[100];

		va_start(args, format);
		vsprintf(buf, format, args);
		va_end(args);

		gettimeofday(&timev, NULL);
		printf("%d.%ld - [%d] - %s",timev.tv_sec, timev.tv_usec, C.rank, buf);
	}
}


//butler functions
connect_to_server(char *hostname, int port)	
{
    int i, rc, client_socket;
    int optval = 1,optlen;
    struct sockaddr_in listener;
    struct hostent *hp;

    hp = gethostbyname(hostname);
    if (hp == NULL)
    {
		printf("connect_to_server: gethostbyname %s: %s -- exiting\n",
		hostname, strerror(errno));
		exit(99);
    }

    bzero((void *)&listener, sizeof(listener));
    bcopy((void *)hp->h_addr, (void *)&listener.sin_addr, hp->h_length);
    listener.sin_family = hp->h_addrtype;
    listener.sin_port = htons(port);

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    error_check(client_socket, "net_connect_to_server socket");

    rc = connect(client_socket,(struct sockaddr *) &listener, sizeof(listener));
	if (rc == -1)
	{
		//printf("Connect with RC -1\n");
		for (i = 0; i < 20; i++)
		{
			tsprintf("Retry connect to %d\n", i + 1);
			if (connect(client_socket,(struct sockaddr *) &listener, sizeof(listener)) == 0)
			{
				//tsprintf("Gotit\n");
				break;
			}
			else
				sleep(i/4);
		}
	}
	error_check(client_socket, "net_connect_to_server connect");

	setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, (char*) &optval, sizeof(optval));	
    setsockopt(client_socket,IPPROTO_TCP,TCP_NODELAY,
	(char *)&optval,sizeof(optval));

    return(client_socket);
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
    int optval = 1, optlen;
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


	setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, (char*) &optval, sizeof(optval));	
    setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&optval, sizeof(optval));

	//tsprintf("NS = %d\n", new_socket);
    return(new_socket);
}





