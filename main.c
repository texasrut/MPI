
#include "mpi.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define MPI_LOCK_EXCLUSIVE 0

//passed
int fffmain(int argc, char** argv) //remote memory WITH LOCKS -- counting test
{
    MPI_Init(NULL, NULL);

    int size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int home = 0;

	
	if(rank == 0)
	{
		sleep(1);

		int out;
		out = 5556;
		MPI_Send(&out, 1 , MPI_INT, 1, 60, MPI_COMM_WORLD);
	}
	else if(rank == 1)
	{
		MPI_Status st;
		MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
		int buf;
		MPI_Recv(&buf, 1 , MPI_INT, st.MPI_SOURCE, st.MPI_TAG, MPI_COMM_WORLD, NULL);
		fprintf(stderr, "it was %d\n", buf);
	}

    MPI_Finalize();

    printf("SUCCESS\n"); fflush(0);
    return 0;
}


//passed
int dddmain(int argc, char** argv) //remote memory WITH LOCKS -- counting test
{
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int home = 0;
	int i;

	int iters = 5;

	int buf[rank];

	MPI_Win win;

	MPI_Win_create(&buf, sizeof(int) * 512,sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &win);

	int data;

	if(rank == 0)
		buf[0] = 0;

	MPI_Barrier(MPI_COMM_WORLD);

	if(rank != 0)
		for(i = 0; i < iters; i++)
		{

			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);

			MPI_Get(&data, 1, MPI_INT, 0, 0, 1, MPI_INT, win);
			data++;
			MPI_Put(&data, 1, MPI_INT, 0, 0, 1, MPI_INT, win);

			MPI_Win_unlock(0, win);
		}

	MPI_Barrier(MPI_COMM_WORLD);

	if(rank == 0)
	{
		printf("Was: %d\n", buf[0]);
	}

	MPI_Finalize();

	printf("SUCCESS\n"); fflush(0);
	return 0;
}

//passed
int mainsss(int argc, char** argv) //remote memory WITH LOCKS
{
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int home = 0;
	int i;

	int buf[rank];

	MPI_Win win;

	MPI_Win_create(&buf, sizeof(int) * 512,sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &win);

	for(i = 0; i < size; i++)
	{
		printf("Trying to acquire\n"); fflush(0);
		MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
		printf("Got it .. holding on for 2 seconds\n"); fflush(0);
		sleep(2);
		printf("Releasing.. done\n"); fflush(0);
		MPI_Win_unlock(0, win);
	}

	MPI_Finalize();

	printf("SUCCESS\n"); fflush(0);
	return 0;
}

int ssssssmain(int argc, char** argv) //remote memory
{
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int home = 0;
	int i, k;

	int buf[rank];

	MPI_Win win;
	
	MPI_Win_create(&buf, sizeof(int) * 512,sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &win);

	for(i = 0; i < size; i++)
	{
		int data = 500 + (rank * i);
		MPI_Put(&data, 1, MPI_INT, i, rank, 1, MPI_INT, win);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	if(rank == 0)
	{
		int results[size];

		for(i = 0; i < size; i++)
		{
			MPI_Get(&results, size, MPI_INT, i, 0, size, MPI_INT, win);
		
			printf("Rank %i had: ", i);
			for(k = 0; k < size; k++)
				printf("%d ", results[k]);
			printf("\n");
		}
	}

	

	MPI_Finalize();

	printf("SUCCESS\n"); fflush(0);
	return 0;
}

//passed
int dddddmain(int argc, char** argv) //nonblocking send/recv
{
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int home = 0;

	MPI_Status status;


	printf("Rank: %d\tSize:%d\n", rank, size); fflush(0);

	if(rank == 0)
	{
		int flag;

		MPI_Request req1, req2, req3;
		MPI_Status st1, st2, st3;

		int buf1 = 1111;
		int buf2 = 2222;
		int buf3 = 3333;

		MPI_Isend(&buf1, 1, MPI_INT, 1, 50, MPI_COMM_WORLD, &req1);
		MPI_Isend(&buf2, 1, MPI_INT, 1, 60, MPI_COMM_WORLD, &req2);
		MPI_Isend(&buf3, 1, MPI_INT, 1, 50, MPI_COMM_WORLD, &req3);

		do{
			MPI_Test(&req1, &flag, &st1);
		}
		while(!flag);

		//MPI_Wait(&req1, &st1);
		MPI_Wait(&req2, &st2);
		MPI_Wait(&req3, &st3);

		fprintf(stderr,"I'm done with the first stage\n");
	}
	if(rank == 1)
	{
		int flag;

		MPI_Request req1, req2, req3;
		MPI_Status st1, st2, st3;

		int buf1, buf2, buf3;

		MPI_Irecv(&buf2, 1, MPI_INT, 0, 60, MPI_COMM_WORLD, &req2); //notice how 60 is out of order
		MPI_Irecv(&buf1, 1, MPI_INT, 0, 50, MPI_COMM_WORLD, &req1);
		MPI_Irecv(&buf3, 1, MPI_INT, 0, 50, MPI_COMM_WORLD, &req3);


		MPI_Wait(&req1, &st1);
		MPI_Wait(&req2, &st2);
		MPI_Wait(&req3, &st3);

		fprintf(stderr,"Stage 1 Recevied: %d %d %d\n", buf1, buf2, buf3);
	}
	sleep(1);

	//stage 2
	if(rank == 0)
	{
		int flag;

		MPI_Request req1, req2, req3;
		MPI_Status st1, st2, st3;

		int buf1 = 1111;
		int buf2 = 2222;
		int buf3 = 3333;

		MPI_Isend(&buf1, 1, MPI_INT, 1, 10, MPI_COMM_WORLD, &req1);
		MPI_Isend(&buf2, 1, MPI_INT, 1, 20, MPI_COMM_WORLD, &req2);
		MPI_Isend(&buf3, 1, MPI_INT, 1, 30, MPI_COMM_WORLD, &req3);

		MPI_Wait(&req1, &st1);
		MPI_Wait(&req2, &st2);
		MPI_Wait(&req3, &st3);

		fprintf(stderr,"I'm done with the second stage\n");
	}
	if(rank == 1)
	{
		int flag;

		MPI_Request req1, req2, req3;
		MPI_Status st1, st2, st3;

		int buf1, buf2, buf3;

		MPI_Irecv(&buf1, 1, MPI_INT, 0, 20, MPI_COMM_WORLD, &req1); //2222
		MPI_Irecv(&buf2, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &req2); // 1111
		MPI_Irecv(&buf3, 1, MPI_INT, 0, 30, MPI_COMM_WORLD, &req3); // 3333

		MPI_Wait(&req1, &st1);
		MPI_Wait(&req2, &st2);
		MPI_Wait(&req3, &st3);

		fprintf(stderr,"Stage 2 Recevied: %d %d %d\n", buf1, buf2, buf3); //2222 1111 3333
	}
	sleep(1);




	//stage 3
	if(rank == 0)
	{
		int flag;

		MPI_Request req1, req2, req3;
		MPI_Status st1, st2, st3;

		int buf1 = 1111;
		int buf2 = 2222;
		int buf3 = 3333;

		MPI_Isend(&buf1, 1, MPI_INT, 1, 10, MPI_COMM_WORLD, &req1);
		MPI_Isend(&buf2, 1, MPI_INT, 1, 20, MPI_COMM_WORLD, &req2);
		MPI_Send(&buf3, 1, MPI_INT, 1, 30, MPI_COMM_WORLD);

		MPI_Wait(&req1, &st1);
		MPI_Wait(&req2, &st2);

		fprintf(stderr,"I'm done with the third stage\n");
	}
	if(rank == 1)
	{
		int flag;

		MPI_Request req1, req2, req3;
		MPI_Status st1, st2, st3;

		int buf1, buf2, buf3;

		MPI_Irecv(&buf1, 1, MPI_INT, 0, 20, MPI_COMM_WORLD, &req1); //2222
		MPI_Irecv(&buf2, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &req2); // 1111
		MPI_Irecv(&buf3, 1, MPI_INT, 0, 30, MPI_COMM_WORLD, &req3); // 3333

		MPI_Wait(&req1, &st1);
		MPI_Wait(&req2, &st2);
		MPI_Wait(&req3, &st3);

		fprintf(stderr,"Stage 3 Recevied: %d %d %d\n", buf1, buf2, buf3); //2222 1111 3333
	}
	sleep(1);



	//stage 4
	if(rank == 0)
	{
		int flag;

		MPI_Request req1, req2, req3;
		MPI_Status st1, st2, st3;

		int buf1 = 1111;
		int buf2 = 2222;
		int buf3 = 3333;

		MPI_Isend(&buf1, 1, MPI_INT, 1, 10, MPI_COMM_WORLD, &req1);
		MPI_Isend(&buf2, 1, MPI_INT, 1, 20, MPI_COMM_WORLD, &req2);
		MPI_Send(&buf3, 1, MPI_INT, 1, 30, MPI_COMM_WORLD);

		MPI_Wait(&req1, &st1);
		MPI_Wait(&req2, &st2);

		fprintf(stderr,"I'm done with the fourth stage\n");
	}
	if(rank == 1)
	{
		int flag;

		MPI_Request req1, req2, req3;
		MPI_Status st1, st2, st3;

		int buf1, buf2, buf3;

		MPI_Irecv(&buf1, 1, MPI_INT, 0, 20, MPI_COMM_WORLD, &req1); //2222
		MPI_Irecv(&buf2, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &req2); // 1111
		MPI_Recv(&buf3, 1, MPI_INT, 0, 30, MPI_COMM_WORLD, &st3); // 3333

		MPI_Wait(&req1, &st1);
		MPI_Wait(&req2, &st2);

		fprintf(stderr,"Stage 4 Recevied: %d %d %d\n", buf1, buf2, buf3); //2222 1111 3333
	}
	sleep(1);


	MPI_Finalize();

	printf("SUCCESS\n"); fflush(0);
	return 0;
}

//passed
int rrrrrrmain(int argc, char** argv) //gather
{
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int home = 0;

	int iter, i;

	printf("Rank: %d\tSize:%d\n", rank, size); fflush(0);

	for(iter = 0; iter < size; iter++)
	{
		home = iter;

		int sbuf = (100 * iter) + rank;

		if(rank == home)
		{
			int rbuf[size * sizeof(int)];
			printf("iter %d\n", iter); fflush(0);

			MPI_Gather(&sbuf, 1, MPI_INT, &rbuf, 1, MPI_INT, home, MPI_COMM_WORLD);

			for(i = 0; i < size; i++)
			{
				int test = rbuf[i];


				//printf("***********************************************From %d got %d   iter %d\n", i, buf,iter); fflush(0);
				if(test != (100 * iter) + i)
				{
					printf("EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL!!!!!!\n"); fflush(0);
					printf("***************************From %d got %d   iter %d\n", i, test, iter); fflush(0);
					exit(1);
				}
			}
		} else {
			MPI_Gather(&sbuf, 1, MPI_INT, NULL, NULL, NULL, home, MPI_COMM_WORLD);
		}
	}

	//printf("GONNA FINISH UP!!!\n"); fflush(0);

	MPI_Finalize();

	printf("SUCCESS\n"); fflush(0);
	return 0;
}

int brianisawesomemain(int argc, char** argv) //Comm Dup Test && ATTRs get put
{
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int com1;
	int com2;

	com1 = MPI_COMM_WORLD;

	if(rank == 0)
	{
		fprintf(stderr,"Com1 Setting 100=>123\n");
		int val = 123;
		MPI_Attr_put(com1, 100, &val);
	}
	MPI_Comm_dup(com1, &com2);

	int com1temp,com2temp;

	MPI_Attr_get(com1, 100, &com1temp, NULL);
	MPI_Attr_get(com2, 100, &com2temp, NULL);


	fprintf(stderr,"Com1 Attr(100)=%d ----- Com2 Attr(100)=%d\n", com1temp, com2temp);

	sleep(1); //for readability

	if(rank == 1) //use a different rank this time
	{
		fprintf(stderr,"Com2 Setting 100=>456\n");
		int val = 456;
		MPI_Attr_put(com2, 100, &val);
		fprintf(stderr,"Com2 Setting 200=>999\n");
		val = 999;
		MPI_Attr_put(com2, 200, &val);
	}

	MPI_Barrier(com2); //make sure we all get through the engine and at least to this point

	int extra;

	MPI_Attr_get(com1, 100, &com1temp, NULL);
	MPI_Attr_get(com2, 100, &com2temp, NULL);
	MPI_Attr_get(com2, 200, &extra, NULL);

	fprintf(stderr,"Com1 Attr(100)=%d ----- Com2 Attr(100)=%d  Attr(200)=%d\n", com1temp, com2temp, extra);
	
	MPI_Finalize();
	return 0;
}

//passed
int dddddddddddmain(int argc, char** argv) //ANY source / ANY tag
{
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	int iter;

	for(iter = 0; iter < 10; iter++)
	{

		int ball = 0;

		if(rank == 0)
		{
			ball++;
			printf("Throwing the ball first Ball balue: %d - iter %d\n", ball, iter); fflush(0);
			//usleep(200*1000);
			MPI_Send(&ball, 1, MPI_INT, rank + 1, 34, MPI_COMM_WORLD);

		} else {
			MPI_Recv(&ball, 1, MPI_INT, rank - 1, 34, MPI_COMM_WORLD, NULL);
			//printf("Got the ball! New value: %d\n", ++ball); fflush(0);
			if(rank != size - 1)
			{
				//usleep(200*1000);
				MPI_Send(&ball, 1, MPI_INT, rank + 1, 34, MPI_COMM_WORLD);
			} else {
				//printf("last guy!\n"); fflush(0);
			}
		}
		MPI_Barrier(MPI_COMM_WORLD);

	}

	printf("all done\n"); fflush(0);
	MPI_Finalize();
	printf("SUCCESS\n"); fflush(0);
	return 0;
}

//passed
int eeeeeeeeemain(int argc, char** argv) //bcast
{
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int iter;

	for(iter = 0; iter < size; iter++)
	{

		int ball;

		ball = (100 * iter) + rank;

		if(rank == iter)
		{
			printf("Broadcasting ball: %d - iter %d\n", ball, iter); fflush(0);
			MPI_Bcast(&ball, 1, MPI_INT, iter, MPI_COMM_WORLD);
		} else {
			MPI_Bcast(&ball, 1, MPI_INT, iter, MPI_COMM_WORLD);
			printf("Got the ball! Value: %d\n", ball); fflush(0);
		}
	}

	printf("all done\n"); fflush(0);
	MPI_Finalize();
	printf("SUCCESS\n"); fflush(0);
	return 0;
}

//passecd
int mssssssssssainBall(int argc, char** argv) //ball
{
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int iter;

	for(iter = 0; iter < 10; iter++)
	{

		int ball = 0;

		if(rank == 0)
		{
			ball++;
			printf("Throwing the ball first Ball balue: %d - iter %d\n", ball, iter); fflush(0);
			//usleep(200*1000);
			MPI_Send(&ball, 1, MPI_INT, rank + 1, 34, MPI_COMM_WORLD);

		} else {
			MPI_Recv(&ball, 1, MPI_INT, rank - 1, 34, MPI_COMM_WORLD, NULL);
			//printf("Got the ball! New value: %d\n", ++ball); fflush(0);
			if(rank != size - 1)
			{
				//usleep(200*1000);
				MPI_Send(&ball, 1, MPI_INT, rank + 1, 34, MPI_COMM_WORLD);
			} else {
				//printf("last guy!\n"); fflush(0);
			}
		}
		MPI_Barrier(MPI_COMM_WORLD);

	}

	printf("all done\n"); fflush(0);
	MPI_Finalize();
	printf("SUCCESS\n"); fflush(0);
	return 0;
}

//passed
int maindddddddddd(int argc, char** argv) //barrier
{
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	
	int iter;

	//printf("Rank: %d\tSize:%d\n", rank, size); fflush(0);

	for(iter = 0; iter < size; iter++)
	{

		if(rank == iter)
		{
			printf("Before barrier, sleeping iter %d\n", iter); fflush(0);
			usleep(100*1000);
			printf("Waking up! %d\n", iter); fflush(0);
			MPI_Barrier(MPI_COMM_WORLD);
			
		} else {
			//printf("waiting\n"); fflush(0);
			MPI_Barrier(MPI_COMM_WORLD);
		}
		printf("barrier done iter %d\n", iter); fflush(0);
	}

	MPI_Finalize();

	printf("SUCCESS\n"); fflush(0);
	return 0;
}

//passed
int mainfffffffffffffffffffffffff(int argc, char** argv) //callhome
{	
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int home = 0;

	MPI_Status status;
	int iter;
	int i;


	printf("Rank: %d\tSize:%d\n", rank, size); fflush(0);

	for(iter = 0; iter < size; iter++)
	{
		home = iter;
		
		if(rank == home)
		{
			int buf;
			printf("iter %d\n", iter); fflush(0);
			for(i = 0; i < size; i++)
			{
				if(i == home)
					continue;
				
				buf = 123;
				//printf("Waiting on %d\n", i); fflush(0);

				//pick only 1 of the next two lines to test different functionality
				//MPI_Recv(&buf, 1, MPI_INT, i, 55, MPI_COMM_WORLD, &status);
				MPI_Recv(&buf, 1, MPI_INT, i, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

				//notice: if you do MPI_ANY_SOURCE THEN YOU SHOULD EXPECT AN ERROR!
				//because this code will throw an EPIC FAIL if it determines that it received the
				//wrong message - obviously an MPI_ANY_SOURCE will receive msgs out of order and do an EPIC FAIL
				//just as it should

				//printf("***********************************************From %d got %d   iter %d\n", i, buf,iter); fflush(0);
				if(buf != (100 * iter) + i)
				{
					printf("EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL EPIC FAIL!!!!!!"); fflush(0);
					printf("		**************************************From %d got %d   iter %d\n", i, buf,iter); fflush(0);
					exit(1);
				}
			}
		} else {
			int data = (100 * iter) + rank;
			//printf("Going\n"); fflush(0);
			MPI_Send(&data, 1, MPI_INT, home, 55, MPI_COMM_WORLD);
			//printf("Sent\n"); fflush(0);
			//printf("Sent for iter %d\n", iter); fflush(0);
		}
	}

	//printf("GONNA FINISH UP!!!\n"); fflush(0);

	MPI_Finalize();

	printf("SUCCESS\n"); fflush(0);
	return 0;
}

//passed
int mainMulti(int argc, char** argv) //strings and multiple ints (and MPI_Abort) and MPI_Status
{
	MPI_Init(NULL, NULL);

	int size;
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	printf("Rank: %d\tSize:%d\n", rank, size); fflush(0);

	if(size < 3)
	{
		fprintf(stderr, "This app requires at least 3 nodes to run!\n");

		//this next if statement is used to prove that MPI_Abort works
		//(if all of the processes called MPI_Abort together what would it prove? nothing!)
		if(rank==0)
			MPI_Abort(MPI_COMM_WORLD, 100);
	}

	if(rank==0)
	{
		int buf[3];
		char sbuf[50];
		MPI_Status status;

		MPI_Recv(&buf, 3, MPI_INT, 1, 60, MPI_COMM_WORLD, &status);
		printf("Recvd %d %d %d Count %d\n", buf[0], buf[1], buf[2], status.count); fflush(0);

		MPI_Recv(&sbuf, 50, MPI_CHAR, 2, 70, MPI_COMM_WORLD, &status);

		printf("Recvd %s  Count %d\n", &sbuf, status.count); fflush(0);

		
	} else if (rank==1)
	{
		int buf[3] = {14, 56, -1};
		MPI_Send(&buf, 3, MPI_INT, 0, 60, MPI_COMM_WORLD);
		printf("sent\n"); fflush(0);
	} else if (rank==2)
	{
		char sbuf[50] = "Hello world!";
		MPI_Send(&sbuf, 50, MPI_CHAR, 0, 70, MPI_COMM_WORLD);
		printf("sent\n"); fflush(0);
	}

	MPI_Finalize();

	printf("SUCCESS\n"); fflush(0);
	return 0;
}

#define BALL_VEC_SZ 100000
int main() //ball with large message
{
    MPI_Init(NULL, NULL);
    int size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    int ball[BALL_VEC_SZ];
    MPI_Status st;
	int iter;

    ball[0] = 0;
    for (iter = 0; iter < 10; iter++)
    {
        if (rank == 0)
        {
            ball[0] = ball[BALL_VEC_SZ-1] = ball[0]++;
            printf("Throwing the ball first Ball value: %d - iter %d\n", ball[0], iter);
            //usleep(200*1000);
            MPI_Send(ball, BALL_VEC_SZ, MPI_INT, rank + 1, 34, MPI_COMM_WORLD);
        } else {
            MPI_Recv(ball, BALL_VEC_SZ, MPI_INT, rank - 1, 34, MPI_COMM_WORLD, &st);
            printf("Got the ball! value: %d %d\n", ball[0], ball[BALL_VEC_SZ-1]);
            if (rank != size - 1)
            {
                //usleep(200*1000);
                MPI_Send(ball, BALL_VEC_SZ, MPI_INT, rank + 1, 34, MPI_COMM_WORLD);
            } else {
                //printf("last guy!\n");
            }
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    printf("all done\n");
    MPI_Finalize();
    printf("SUCCESS\n");
    return 0;
}
