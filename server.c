//Includes
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <ctype.h>
#include <pthread.h>
#include <ifaddrs.h>


//Client's structs
#pragma pack(1)
typedef struct
{
	uint8_t commandType;//0
	uint16_t reserved;
}Hello;
#pragma pack(1)
typedef struct
{
	uint8_t commandType;//1
	uint16_t stationNumber;
}AskSong;
#pragma pack(1)
typedef struct
{
	uint8_t commandType;//2
	uint32_t songSize;//in bytes
	uint8_t songNameSize;
	char* songName;
}UpSong;

//Server's structs
#pragma pack(1)
typedef struct
{
	uint8_t replyType;//0
	uint16_t numStations;
	uint32_t multicastGroup;
	uint16_t portNumber;
}Welcome;
#pragma pack(1)
typedef struct
{
	uint8_t replyType;//1
	uint8_t songNameSize;
	char* songName;
}Announce;
#pragma pack(1)
typedef struct
{
	uint8_t replyType;//2
	uint8_t permit;
}PermitSong;
#pragma pack(1)
typedef struct
{
	uint8_t replyType;//3
	uint8_t replyStringSize;
	char* replyString;
}InvalidCommand;
#pragma pack(1)
typedef struct
{
	uint8_t replyType;//4
	uint16_t newStationNumber;
}NewStations;

typedef struct client{
	int socket_fd;
	int thread;
	int state;
}client;

//timeout defines
#define HELLO_TIMEOUT 300000 //300 Milliseconds
#define ASKSONG_TIMEOUT 300000 //300 Milliseconds
#define UP_TIMEOUTE 300000 //300 Milliseconds

//Defines
#define MAX_CLIENTS 100
#define DEFAULT_VALUE 0
#define UDP_MULTICAST_RATE 62500
#define UPSONG_TIMEOUT 3;

//Cases of termination
#define ERROR_NOT_ENOUGH_ARGUMENTS 1
#define ERROR_OPENING_FILE 2
#define ERROR_OPENING_FILE_MP3 3
#define TERMINATE 4
#define ERROR_OPPENNING_SOCKET 5
#define ERROR_BIND_SOCKET 6
#define ERROR_LISTEN 7
#define ERROR_READ 8
#define ERROR_SETSOCKET 9
#define ERROR_READ_FILE 10
#define ERROR_SENDING_MESSAGE 11
#define ERROR_ACCEPT 12
#define ERROR_SELECT 13

//user cases termination
#define ERROR_SETOPT_USER 1
#define ERROR_TIMEOUT 2
#define ERROR_SEND 3
#define ERROR_WRONG_MESSAGE_TYPE 4
#define ERROR_SONG_NUMBER 5


enum CMessageNum {HELLO = 0,ASKSONG,UPSONG,CHANGE_STATION,EXIT};
enum SMessageNum {WELCOME = 0,ANNOUNCE,PERMITSONG,INVALIDCOMMAND,NEWSTATIONS};

//initialize default value for Hello message
#define INIT_HELLO(X) Hello X = {X.commandType = HELLO, X.reserved = 0}
//initialize default value for AskSong message
#define INIT_ASKSONG(X) AskSong X = {X.commandType = ASKSONG, X.stationNumber =  DEFAULT_VALUE}
//initialize default value for UpSong message
#define INIT_UPSONG(X) UpSong X = { X.commandType = UPSONG_PERMITSONG,X.songSize =  DEFAULT_VALUE,X.songNameSize =  DEFAULT_VALUE}
//initialize default value for Welcome message
#define INIT_WELCOME(X) Welcome X = {X.replyType = WELCOME,X.multicastGroup = multicast_addr,X.numStations = ntohs(num_of_songs),X.portNumber = ntohs(UDP_port)}
//initialize default value for PermitSong message
#define INIT_PERMITSONG(X) PermitSong X = {X.replyType = PERMITSONG, X.permit = DEFAULT_VALUE}
//initialize default value for NewStations message
#define INIT_NEWSTATIONS(X) NewStations X = {X.replyType = NEWSTATIONS,X.newStationNumber = DEFAULT_VALUE}
//send + validity check
#define SEND(X,Y)  if(write(client_list[user_index].socket_fd, &X, Y) < Y ) clean_exit_single_user(user_index,ERROR_SEND);
//read + validity check
#define READ(X,Y,Z)  if(read(client_list[user_index].socket_fd, &X, Y) < Y || X.commandType != Z) clean_exit_single_user(user_index,ERROR_READ);
//increment an IP X by Y addreses
#define INCR_IP(X,Y) station_sockets.sin_addr.s_addr = htonl(ntohl(X) + Y);

//Function deceleration
void clean_exit(int type,char* msg);
void* Keyboard_input_thread();
void Song_FD_update(char* argv[]);
void* client_interaction(int user_index);
void clean_exit_single_user(int user_index,int type, char* msg);
void* station_multicast();
void new_station_to_all();
void add_new_song(char* name,int size,FILE *fp);

//Global variables
int TCP_port,terminate = 0, ttl = 100,permit = -1,num_of_users = 0;
unsigned int multicast_addr;
unsigned short UDP_port, num_of_songs;
int welcome_socket_FD;
int *radio_socketFD;
int *song_name_size_arr;
//int *clientFD[MAX_CLIENTS];
FILE** song_FD;
char** song_name_array;
client client_list[MAX_CLIENTS];
struct ip_mreq* multicast_adresses;
struct sockaddr_in* station_sockets;
struct sockaddr_in welcome_socket;
struct sockaddr_in new_client_sockaddr[MAX_CLIENTS];
Hello hello_message;

pthread_t keyboard_thread;
pthread_t multicast_thread;
pthread_t client_threads[MAX_CLIENTS];
pthread_mutex_t upload_song;
pthread_mutex_t new_station_mutex;
Welcome welcome ;

/*
 * client state :   -2 not active
 *					-1 before established
 *				from 0 to n -> station number
 *
 *
 *
 */
int main(int argc, char* argv[]){

	//arguments <TCP Port> <Multicast address> <UDP Port> <Song1>..
	int size,i;
	struct timeval timeout;
	//checking input arguments
	if(argc < 5 )
		clean_exit(ERROR_NOT_ENOUGH_ARGUMENTS,"argc check..\n");

	num_of_songs = argc - 4;
	TCP_port = atoi(argv[1]);
	UDP_port = atoi(argv[3]);
	multicast_addr = inet_addr(argv[2]);
	printf("TCP port is: %d, UDP port is: %d, multicast address is:%s.\n",TCP_port,UDP_port,argv[2]);

	//initializing messages
	welcome.multicastGroup = htonl(multicast_addr);
	welcome.numStations = htons(num_of_songs);
	welcome.portNumber = htons(UDP_port);
	welcome.replyType = 0;
	hello_message.commandType = 0;
	hello_message.reserved = 0;

	Song_FD_update(argv);

	//-2 not active
	//-1 before established
	//0,1,2...num of station
	for(i = 0;i<MAX_CLIENTS;i++)
		client_list[i].state = -2;

	//run keyboard thread
	pthread_create(&keyboard_thread,NULL,Keyboard_input_thread,NULL);
	pthread_create(&multicast_thread,NULL,station_multicast,NULL);

	//setting up welcome socket
	welcome_socket.sin_addr.s_addr = INADDR_ANY;
	welcome_socket.sin_family = AF_INET;
	welcome_socket.sin_port = htons(TCP_port);

	if((welcome_socket_FD  = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) < 0 )
		clean_exit(ERROR_OPPENNING_SOCKET,"Openning welcome socket..\n");
	if((bind(welcome_socket_FD,(struct sockaddr*) &welcome_socket ,sizeof(welcome_socket))) < 0)
		clean_exit(ERROR_BIND_SOCKET,"Binding welcome socket\n");
	if(listen(welcome_socket_FD,MAX_CLIENTS) < 0 ) //set max queue length
			clean_exit(ERROR_LISTEN,"while setting max user in listen func\n");

	int new_client_socket,user_index,flag;

	//-----------------welcome socket handling---------------
	while(terminate != 1){

		printf("Waiting for new clients..\n");

		timeout.tv_sec = 0;
		timeout.tv_usec = HELLO_TIMEOUT;
		fd_set monitored;
		FD_ZERO(&monitored);

		size  = sizeof(new_client_sockaddr[0]);
		//search for free spot
		for(i = 0; i<MAX_CLIENTS;i++)
			if(client_list[i].state == -2)
				break;

		if((new_client_socket = accept(welcome_socket_FD,(struct sockaddr*)&new_client_sockaddr[i],&size)) < 0)
			clean_exit(ERROR_ACCEPT,"While accepting users..\n");


		if(i == MAX_CLIENTS){
			printf("Oops some one tried to connect and filed because its not enough room for new users..\n ");
			continue;
		}

		//setting new client

		client_list[i].socket_fd = new_client_socket;
		client_list[i].state = 0;
		user_index =i;

		FD_SET(client_list[user_index].socket_fd,&monitored);
		size = sizeof(hello_message);

		//waiting for hello message
		if((flag = select(client_list[user_index].socket_fd+1,&monitored,NULL,NULL,&timeout)) < 0)
			clean_exit_single_user(user_index,ERROR_SELECT,"while waiting to hello message\n");
		else if(flag == 0)
			clean_exit_single_user(user_index,ERROR_TIMEOUT,"TIMEOUT while waiting to hello\n");

		if(read(client_list[user_index].socket_fd, &hello_message, size) < 0)
			clean_exit_single_user(user_index,ERROR_READ,"while reading hello message\n");
		if(hello_message.commandType != 0)
			clean_exit_single_user(user_index,ERROR_WRONG_MESSAGE_TYPE,"Wrong type of message while waiting for hello,\n");

		size = sizeof(welcome);

		if(write(client_list[user_index].socket_fd, &welcome, size) < 0)
			clean_exit_single_user(user_index,ERROR_SEND,"ERROR while sending welcome to user");

		pthread_create(&client_threads[i],NULL,client_interaction,i);

	}

	return 0;
}

//this function will initialize or update all variable for maintaining song list
void Song_FD_update(char* argv[]){
	int i,song_name_size;
	char* j;
	//initialize song FD & song name array
		song_FD = (FILE**)calloc(num_of_songs,sizeof(FILE*));
		song_name_array = (char**)calloc(num_of_songs,sizeof(char*));
		station_sockets = (struct sockaddr_in*)calloc(num_of_songs,sizeof(struct sockaddr_in));
		multicast_adresses = (struct ip_mreq*)calloc(num_of_songs,sizeof(struct ip_mreq));
		radio_socketFD = (int*)calloc(num_of_songs,sizeof(int));
		song_name_size_arr = (int*)calloc(num_of_songs,sizeof(int));

		//init song name array && FD
		for(i = 0;i<num_of_songs;i++){
			//open files and getting file names
			song_name_array[i] = argv[i+4];
			j = strchr(song_name_array[i],'.');
			song_name_size = j-song_name_array[i];
			song_name_size_arr[i] = song_name_size+4;
			if(song_name_array[i][song_name_size+3] != '3' || song_name_array[i][song_name_size+2] != 'p'||song_name_array[i][song_name_size+1] != 'm' || song_name_array[i][song_name_size] != '.')
				clean_exit(ERROR_OPENING_FILE_MP3,"it is not a mp3 format file\n");
			song_FD[i] = fopen(song_name_array[i],"r+");
			if(song_FD[i] == NULL){
				clean_exit(ERROR_OPENING_FILE,"ERROR while trying to open  mp3 file\n");
			}
			//now we will set multicast addresses for each song
			station_sockets[i].sin_family = AF_INET;
			station_sockets[i].sin_port = htons(UDP_port);
			station_sockets[i].sin_addr.s_addr =htonl(ntohl(inet_addr(argv[2])) + i);
			multicast_adresses[i].imr_interface.s_addr = INADDR_ANY;
			multicast_adresses[i].imr_multiaddr.s_addr = htonl(ntohl(inet_addr(argv[2])) + i);

			if((radio_socketFD[i] = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)) < 0)
				clean_exit(ERROR_OPPENNING_SOCKET,"while opening socket for UDP station\n");
			if(bind(radio_socketFD[i],(struct sockaddr*)&station_sockets[i],sizeof(station_sockets[i])))
				clean_exit(ERROR_BIND_SOCKET,"ERROR binding UDP socket");
			if(setsockopt(radio_socketFD[i],IPPROTO_IP,IP_MULTICAST_TTL,&ttl,sizeof(ttl)) <-1)
				clean_exit(ERROR_SETSOCKET,"while trying to set TTL..\n");
			if(setsockopt(radio_socketFD[i],IPPROTO_IP,IP_ADD_MEMBERSHIP,&(multicast_adresses[i]),sizeof(multicast_adresses[i])) < -1)
				clean_exit(ERROR_SETSOCKET,"ERROR setsocketopt ip add membership\n");

		}//for

}
/*this thread function is meant to read input from the keyboard
valid keyboard input:
q-quit
p-print info
*/
void* Keyboard_input_thread(){

	int i;
	char buffer[128];
	printf("Keyboard thread created..\n");
		//running endlessly waiting for input
		while(1){

			fflush(stdin);
			fgets(buffer,sizeof(buffer)-1,stdin);


			if(buffer[0] == 'q' && buffer[1] == '\n')
				clean_exit(TERMINATE,"clean exit\n");

			if(buffer[0] == 'p' && buffer[1] == '\n'){

				for(i = 0; i<num_of_songs;i++){
					printf("station number: %d -> song playing: %s\n" , i,song_name_array[i]);
				}
				int num = 1;
				for(i = 0; i <MAX_CLIENTS;i++){
					if(client_list[i].state != -2){
						printf("Client number: %d ip address: %s\n",num, inet_ntoa(new_client_sockaddr[i].sin_addr));
						num++;
					}
			}
		}
}
void* station_multicast(){
	int i,num_of_byts;
	char buffer[1024];
	printf("multicast thread is running..\n");
	struct sockaddr_in temp;
	temp.sin_port = htons(UDP_port);
	temp.sin_family = AF_INET;

	while(terminate != 1){
		pthread_mutex_lock(&new_station_mutex);



		for(i = 0; i<num_of_songs; i++){

			temp.sin_addr.s_addr = htonl(ntohl(multicast_addr) + i);
			//printf("temp - i = %d : %s\t%d\n",i,inet_ntoa(temp.sin_addr), temp.sin_port);
			//printf("i = %d : %s\t%d\n",i,inet_ntoa(station_sockets[i].sin_addr), station_sockets[i].sin_port);
			//reading from file
			if((num_of_byts = fread(buffer,sizeof(char),sizeof(buffer),song_FD[i])) < 0)
				clean_exit(ERROR_READ_FILE,"ERROR while trying to read from file(transmitter)");
			//sending data to socket
			if(sendto(radio_socketFD[i],buffer,num_of_byts,0,(struct sockaddr*)&temp,sizeof(temp)) < 0)
				clean_exit(ERROR_SENDING_MESSAGE,"while sending bits to UDP socket");
			//checking if we in the end of the file
			if(num_of_byts < sizeof(buffer))
				rewind(song_FD[i]);

			memset(buffer,0,sizeof(buffer));
		}//for
		pthread_mutex_unlock(&new_station_mutex);
	usleep(UDP_MULTICAST_RATE); //set transmition rate
	}//while
}

//thread for each client
void* client_interaction(int user_index){

	int flag,size,read_length,total;
	uint8_t messageType;
	uint16_t station_number;
	char buffer[1024];
	FILE* fp;
	Announce announce;
	PermitSong permit_song;
	UpSong upsong;

	fd_set monitored;
    FD_ZERO(&monitored);
	FD_SET(client_list[user_index].socket_fd,&monitored);

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = HELLO_TIMEOUT;

	printf("user thread with index %d created and connection established..\n",user_index);
	//established
	while(terminate != 1){
		//waiting for messages
		if(select(client_list[user_index].socket_fd+2,&monitored,NULL,NULL,NULL) < 0)
			clean_exit_single_user(user_index,ERROR_SELECT,"ERROR while sitting on select - from user\n");

		if(recv(client_list[user_index].socket_fd, &messageType, sizeof(uint8_t),NULL) < 0)
			clean_exit_single_user(user_index,ERROR_READ,"while trying to receive\n");

		switch(messageType){

			case ASKSONG:

				if(read(client_list[user_index].socket_fd, &station_number, sizeof(uint16_t)) < 0)
					clean_exit_single_user(user_index,ERROR_READ,"while trying to read the rest of askSong message\n");
				station_number = ntohs(station_number);
				if(station_number > num_of_songs || station_number < 0)
					clean_exit_single_user(user_index,ERROR_SONG_NUMBER,"Song number not exist\n");

				announce.replyType = 1;
				announce.songNameSize = song_name_size_arr[station_number];
				announce.songName = song_name_array[station_number];
				size = sizeof(announce)+4;

				//announce.songNameSize = ntohs(announce.songNameSize);
				if(write(client_list[user_index].socket_fd, &announce.replyType, size) < 0)
					clean_exit_single_user(user_index,ERROR_SEND,"while writing announce message\n");
				break;

			case UPSONG:
				size = 5;
				if(read(client_list[user_index].socket_fd, &upsong.songSize, size) < 0)
					clean_exit_single_user(user_index,ERROR_READ,"while reading the rest of askSong message\n");

				if((upsong.songName = (char *)malloc(upsong.songNameSize*sizeof(char))) == NULL)
					clean_exit_single_user(user_index,ERROR_READ,"while trying to allocate memory for the name of the song\n");

				if(read(client_list[user_index].socket_fd, upsong.songName, upsong.songNameSize) < 0)
					clean_exit_single_user(user_index,ERROR_READ,"while trying to read the song name (upSong)\n");

				upsong.songSize = ntohl(upsong.songSize);

				printf("starting upSong procedure..\n");
				pthread_mutex_lock(&upload_song);
				if(permit == -1)
					permit = user_index;
				else
				{
					permit_song.replyType = 2;
					permit_song.permit = 0;
					if(write(client_list[user_index].socket_fd, &permit_song, sizeof(permit_song)) < 0)
						clean_exit_single_user(user_index,ERROR_SEND,"while trying to send permit  = 0\n");
					pthread_mutex_unlock(&upload_song);
					continue;
				}

				pthread_mutex_unlock(&upload_song);

				//starting upload procedure

				size = upsong.songSize;

				if((fp = fopen(upsong.songName,"w")) == 0)
					clean_exit_single_user(user_index,ERROR_OPENING_FILE,"While trying to open new file for new station\n");

				permit_song.replyType = 2;
				permit_song.permit = 1;
				size = sizeof(permit_song);
				if(write(client_list[user_index].socket_fd, &permit_song, size) < 0)
					clean_exit_single_user(user_index,ERROR_SEND,"While trying to send permit 1\n");
				printf("after sending permit..\n");
				//waiting for packets
				total = 0;
			    FD_ZERO(&monitored);
				FD_SET(client_list[user_index].socket_fd,&monitored);

				while(total < upsong.songSize){
					timeout.tv_sec = UPSONG_TIMEOUT;
					timeout.tv_usec = 0;

					if((flag = select(client_list[user_index].socket_fd+1,&monitored,NULL,NULL,&timeout)) < 0)
						clean_exit_single_user(user_index,ERROR_SELECT,"while waiting on select (not timeout) when downloading a song\n ");
					else if(flag == 0)
						clean_exit_single_user(user_index,ERROR_TIMEOUT,"TIMEOUT - while downloading a song\n ");

					if((read_length = read(client_list[user_index].socket_fd, buffer, sizeof(buffer))) < 0)
						clean_exit_single_user(user_index,ERROR_READ,"While trying to read after select indicate that data was recieved (downloading song)\n");

					fwrite(buffer,sizeof(char),read_length,fp);
					total+=read_length;
					fflush(stdout);
					printf("\r progress %d% ",(total*100)/upsong.songSize);

				}
				printf("\n\n\n");

				//update all listeners about new station
				new_station_to_all();
				add_new_song(upsong.songName,upsong.songNameSize,fp);
				pthread_mutex_lock(&upload_song);
				permit = -1;
				pthread_mutex_unlock(&upload_song);
			//	free(upsong.songName);
				break;

			default:
				clean_exit_single_user(user_index,ERROR_WRONG_MESSAGE_TYPE,"Wrong message type or user disconnected\n");
				break;

	}
	}


}
void new_station_to_all(){

	int i,size;
	NewStations new;
	new.newStationNumber = ntohs(num_of_songs);
	new.replyType = 4;
	size = 3;

	for(i = 0;i<MAX_CLIENTS;i++){
		if(client_list[i].state != -2 && i != permit)
			if(write(client_list[i].socket_fd,&new.replyType,size) < 0)
							clean_exit(ERROR_SEND,"While trying to write to all about new station\n");
	}

}

void add_new_song(char* name,int size,FILE *fp){

	pthread_mutex_lock(&new_station_mutex);
	song_FD = (FILE**)realloc(song_FD,num_of_songs+1);
	song_name_array = (char**)realloc(song_name_array,num_of_songs+1);
	radio_socketFD = (int*)realloc(radio_socketFD,num_of_songs+1);

//	new_name = (char*)malloc(sizeof(char)*size);
	//strcpy(new_name,name);

	song_name_array[num_of_songs] = name;
	song_name_size_arr[num_of_songs] = size;
	if(song_name_array[num_of_songs][size-1] != '3' || song_name_array[num_of_songs][size-2] != 'p'||song_name_array[num_of_songs][size-3] != 'm' || song_name_array[num_of_songs][size-4] != '.')
		clean_exit(ERROR_OPENING_FILE_MP3,"File is not mp3 type (new station)\n ");
	song_FD[num_of_songs] =fp;

	struct sockaddr_in temp;
	temp.sin_addr.s_addr = htonl(ntohl(multicast_addr) + num_of_songs);
	temp.sin_port = htons(UDP_port);
	temp.sin_family = AF_INET;
	struct ip_mreq temp1;
	temp1.imr_interface.s_addr = INADDR_ANY;
	temp1.imr_multiaddr.s_addr = htonl(ntohl(multicast_addr) + num_of_songs);
	//now we will set multicast addresses for each song
	//station_sockets[num_of_songs].sin_family = AF_INET;
	//station_sockets[num_of_songs].sin_port = htons(UDP_port);
	//station_sockets[num_of_songs].sin_addr.s_addr =htonl(ntohl(multicast_addr) + num_of_songs);
	//multicast_adresses[num_of_songs].imr_interface.s_addr = INADDR_ANY;
	//multicast_adresses[num_of_songs].imr_multiaddr.s_addr = htonl(ntohl(multicast_addr) + num_of_songs);

	if((radio_socketFD[num_of_songs] = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)) < 0)
		clean_exit(ERROR_OPPENNING_SOCKET,"While trying to open new socket(new station)\n");

	if(bind(radio_socketFD[num_of_songs],(struct sockaddr*)&temp,sizeof(temp)))
		clean_exit(ERROR_BIND_SOCKET,"While trying to bind socket(new station)\n");

	if(setsockopt(radio_socketFD[num_of_songs],IPPROTO_IP,IP_MULTICAST_TTL,&ttl,sizeof(ttl)) <-1)
		clean_exit(ERROR_SETSOCKET,"While trying to set TTL(new station)\n");

	if(setsockopt(radio_socketFD[num_of_songs],IPPROTO_IP,IP_ADD_MEMBERSHIP,&temp1,sizeof(temp1)) < -1)
		clean_exit(ERROR_SETSOCKET,"While trying to add membership(new station)\n");


	printf("%s \n %d",inet_ntoa(station_sockets[num_of_songs].sin_addr),ntohs(station_sockets[num_of_songs].sin_port));

	num_of_songs++;
	//new_station_to_all();

	pthread_mutex_unlock(&new_station_mutex);

}
void clean_exit_single_user(int user_index,int type, char* msg){

	InvalidCommand invalid_comand;
	switch (type) {
			case ERROR_WRONG_MESSAGE_TYPE:
				printf("ERROR_WRONG_MESSAGE_TYPE..\n");
				break;
			case ERROR_SELECT:
				printf("ERROR_SELECT\n");
				break;
			case ERROR_TIMEOUT:
				printf("ERROR_TIMEOUT..\n");
				break;
			case ERROR_READ:
				printf("ERROR_READ..\n");
				break;
			case ERROR_SONG_NUMBER:
				printf("ERROR_SONG_NUMBER..\n");
				break;

			default:
				break;

	}

	invalid_comand.replyType = 3;
	invalid_comand.replyStringSize = strlen(msg);
	invalid_comand.replyString = msg;

	int size = sizeof(invalid_comand);

	if(write(client_list[user_index].socket_fd, &invalid_comand, size) < 0)
		printf("invalid message not sent\n");

	printf("%s\n",msg);
	close(client_list[user_index].socket_fd);
	client_list[user_index].state = -2;
	pthread_exit(EXIT_FAILURE);

}
//this function will print the error cause and preform clean exit
void clean_exit(int type, char* msg){
	//to do - clean exit
	int i;
	//print the cause of failure
	perror(strerror(errno));

	//check the cause of termination
	switch (type) {
		case ERROR_NOT_ENOUGH_ARGUMENTS:
			printf("ERROR not enough arguments mate/n<TCP Port> <Multicast address> <UDP Port> <Song1>..\n");
			exit(1);
			break;
		case ERROR_OPENING_FILE:
			printf("ERROR opening file\n");
			break;
		case ERROR_OPENING_FILE_MP3:
			printf("ERROR opening file - not mp3 file.\n");
			break;
		case TERMINATE:
			printf("Performing exit\n");
			break;
		case ERROR_OPPENNING_SOCKET:
			printf("Couldn't open socket.\n");
			break;
		case ERROR_BIND_SOCKET:
			printf("Couldn't bind socket.\n");
			break;
		case ERROR_LISTEN:
			printf("Listen function error.\n");
			break;
		case ERROR_READ:
			printf("ERROR reading from socket.\n");
			break;
		case ERROR_SETSOCKET:
			printf("ERROR in set socket function.\n");
			break;
		case ERROR_READ_FILE:
			printf("ERROR while reading file.\n");
			break;
		case ERROR_SENDING_MESSAGE:
			printf("ERROR while sending message.\n");
			break;
		case ERROR_ACCEPT:
			printf("ERROR in accept function.\n");
			break;
		default:
			printf("%s\n",msg);
			break;
	}
	//------------------------need to check if i close all the sockets and free al allocated mem--------------------------
	for(i = 0;i<num_of_songs;i++)
		fclose(song_FD[i]);

	free(song_FD);
	free(song_name_array);
	free(multicast_adresses);
	free(station_sockets);
	free(radio_socketFD);

	if(type == TERMINATE){
		terminate = 1;
		while(1);
	}
	exit(1);
}
