
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define DEFAULT_VALUE 99
#define HELLO_TIMEOUT 300000 //300 Milliseconds
#define ASKSONG_TIMEOUT 300000 //300 Milliseconds
#define NEWSTATION_TIMEOUT 2//seconds
#define UP_TIMEOUTE 300000 //300 Milliseconds
#define RATE_CTRL 8000 //8 Milliseconds

#define TRUE 1
#define UDP_CHUNK 1024
#define UPSONG_CHUNK 1024
#define MAX_LEN_SONG_NAME 100
#define MAX_SONG_SIZE 10000000
#define MIN_SONG_SIZE	2000
#define CHANGE_STATION_LEN 70

//cases of termination
enum ErrorTypes {ERROR_CONNECT = 1};
enum CMessageNum {HELLO = 0,ASKSONG,UPSONG,CHANGE_STATION,EXIT};
enum SMessageNum {WELCOME = 0,ANNOUNCE,PERMITSONG,INVALIDCOMMAND,NEWSTATIONS};

//initialize default value for Hello message
#define INIT_HELLO(X) Hello X = {X.commandType = HELLO, X.reserved = 0}
//initialize default value for AskSong message
#define INIT_ASKSONG(X) AskSong X = {X.commandType = ASKSONG, X.stationNumber =  0}
//initialize default value for UpSong message
#define INIT_UPSONG(X) UpSong X = { X.commandType = UPSONG_PERMITSONG,X.songSize =  DEFAULT_VALUE,X.songNameSize =  DEFAULT_VALUE}
//initialize default value for Welcome message
#define INIT_WELCOME(X) Welcome X = {X.replyType = HELLO_WELCOME,X.multicastGroup = DEFAULT_VALUE,X.numStations = DEFAULT_VALUE,X.portNumber = DEFAULT_VALUE}
//initialize default value for PermitSong message
#define INIT_PERMITSONG(X) PermitSong X = {X.replyType = PERMITSONG, X.permit = DEFAULT_VALUE}
//initialize default value for InvalidCommand message
//#define INIT_INVALIDCOMMAND(X,Y) InvalidCommand X = {X.replyType = 3, X.replyStringSize = Y, X.replyString = (char*)malloc(Y*sizeof(char))}
//initialize default value for NewStations message
#define INIT_NEWSTATIONS(X) NewStations X = {X.replyType = NEWSTATIONS,X.newStationNumber = DEFAULT_VALUE}
//increment an IP X by Y addreses
#define INCR_IP(Z,X,Y) Z = htonl(ntohl(X) + Y);

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

/*functiond declaration*/
void clean_exit(char* failMsg);
void state_connection_established();//
int get_integer_from_user(const char* menu,int min, int max);
void get_NewStation_message();//
void ask_song();//
void init_InvalidCommand(InvalidCommand* message);
void listener_exit(int udpSocket, FILE* fp);
void get_invalidCommand_message();
int check_message(char* failMsg);
void check_select(int selectVal,char* failedMsg);
void init_announce(Announce* message);
int upload_song();//
void* listener();

//global variables
uint16_t numOfStations = DEFAULT_VALUE;
struct sockaddr_in multicastAddress;//address of the first multicast group
uint serverSocket = 0;
uint32_t currentStation = 0;
struct sockaddr_in serverAddress;

/*
 * arg[0] - reserved
 * arg[1] - server's address
 * arg[2] - server's TCP port number
 */
int main(int argc,char* argv[]) {

	if (argc < 3) {
		puts("Not enough arguments.\nArgs: <server's IP> <server's TCP port>\n");
		return 0;
	}
	printf("Preparing for connection with server:\n");

	//define the server address
	memset(&serverAddress,0,sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;				//IPv4
	serverAddress.sin_port = htons(atoi(argv[2]));		//port number
	serverAddress.sin_addr.s_addr =inet_addr(argv[1]);	//server's address

	//create socket
	serverSocket = socket(AF_INET,SOCK_STREAM,0);
	if (serverSocket < 0)
		clean_exit("Failed: TCP socket().\n");
	printf("FD socket obtained.\n");

	printf("Send a connection request to serever.\n");
	//send request for a TCP connection
	int connectionFlag;
	connectionFlag = connect(serverSocket,(const struct sockaddr*)&serverAddress,sizeof(serverAddress));
	if( connectionFlag < 0)
		clean_exit("Failed: connect.\n");
	printf("Connection established.\n");

     //create a set of FDs to monitor
    fd_set monitored;
    FD_SET(serverSocket,&monitored);

    //create the timeout struct
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = HELLO_TIMEOUT;

    //send Hello message and wait for a reply
	int size = sizeof(Hello);
	int selectVal;
	INIT_HELLO(hello);

	if ( write(serverSocket, &hello, size) < size )
		clean_exit("Failed to send hello massage.\n");
    printf("Hello message was sent to server.\n");
    selectVal = select(serverSocket+1,&monitored,NULL,NULL,&timeout);
	check_select(selectVal,"waiting for welcome message.\n");
    printf("Got a reply from the server.\n");

    //get a reply
    size = sizeof(Welcome);
    Welcome firstReply;
    if ( read(serverSocket, &firstReply, size) < size || firstReply.replyType != WELCOME){
    	clean_exit("Failed to receive a welcome message after hello message.\n");
    }
    //check if all bytes were received
    printf("The reply is a welcome message.\n");

    //set the information got from the server
    numOfStations =  ntohs(firstReply.numStations);
	multicastAddress.sin_family = AF_INET;				//IPv4
	multicastAddress.sin_port = ntohs(firstReply.portNumber);		//port number
	multicastAddress.sin_addr.s_addr =ntohl(firstReply.multicastGroup);	//server's address
	memset(multicastAddress.sin_zero,0,sizeof(multicastAddress.sin_zero));

    //create new thread to function as a multicast listener
    pthread_t listenerId;
    printf("UDP port num: %d\n",multicastAddress.sin_port);
    printf("Multicast address: %s\n",inet_ntoa(multicastAddress.sin_addr));
    if(pthread_create(&listenerId,NULL,listener,NULL)){
    	clean_exit("Creation of thread:'listener' failed.");
    }

    state_connection_established();
	return 0;
}
void clean_exit(char* failMsg){
	//print the cause of failure - errno
	if(errno)
		perror(strerror(errno));
	//print my error message
	perror(failMsg);
	close(serverSocket);
	exit(1);
}

void check_select(int selectVal,char* failedMsg) {
	switch (selectVal) {
	case -1:
		printf("Select(): %s",failedMsg);
		break;
	case 0:
		printf("Timeout: %s",failedMsg);
		break;
	default:
			return;
	}
	clean_exit("");
}

void listener_exit(int udpSocket, FILE* fp) {
	fclose(fp);
	close(udpSocket);
	pthread_exit(0);
}

void* listener(){
	int udpSocket, listenningTo;
	char buffer[UDP_CHUNK+1] = {0};
	struct ip_mreq multicastGroup;
	//open mp3 player
	FILE* fp= popen("play -t mp3 -> /dev/null 2>%1","w");
	if(!fp){
		puts("LISTENER: Failed opening player");
		pthread_exit(0);
	}

	//create socket
	udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if(udpSocket < 0){
		puts("LISTENER: Failed opening udp socket\n");
		fclose(fp);
		pthread_exit(0);
	}

	struct sockaddr_in addr;
		memset(&addr,0,sizeof(addr));
			addr.sin_family=AF_INET;
			addr.sin_addr.s_addr=htonl(INADDR_ANY); /* N.B.: differs from sender */
			addr.sin_port=htons(multicastAddress.sin_port);//htons(INADDR_ANY);//

	//listen to the server's address and port
	if(bind(udpSocket,(const struct sockaddr*)&addr, sizeof(addr)) < 0){
		puts("LISTENER: Failed binding udp socket\n");
		listener_exit(udpSocket, fp);
	}

	 while(TRUE){
		 INCR_IP(addr.sin_addr.s_addr,multicastAddress.sin_addr.s_addr,currentStation);
		 listenningTo = currentStation;

		 //join the appropriate multicast group
		multicastGroup.imr_multiaddr.s_addr = addr.sin_addr.s_addr;
		multicastGroup.imr_interface.s_addr = htonl(INADDR_ANY);
		if(setsockopt(udpSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &multicastGroup,sizeof(multicastGroup))<0){
			puts("LISTENER: Failed joining multicast group.\n");
			listener_exit(udpSocket, fp);
		}

		//listening to the chosen station
		while(listenningTo == currentStation){
			if ( read(udpSocket, buffer, UDP_CHUNK) < UDP_CHUNK){
				puts("LISTENER: Failed reading a udp chunk\n");
				//listener_exit(udpSocket, fp);
			}

			if(fwrite(buffer,sizeof(char),UDP_CHUNK/sizeof(char),fp) < UDP_CHUNK){
				puts("LISTENER: Failed passing a udp chunk to pipe.\n");
				//listener_exit(udpSocket, fp);
			}

		}//while(listenningTo == currentStation)
		if(setsockopt(udpSocket,IPPROTO_IP,IP_DROP_MEMBERSHIP,&multicastGroup,sizeof(multicastGroup))<0){
			puts("LISTENER: Failed droping from multicast group.\n");
			listener_exit(udpSocket, fp);
		}

	}//while(TURE)
return NULL;

}

void state_connection_established(){
    int input = -1,selectVal,keyboardFD = fileno(stdin);
    int maxFD = serverSocket > keyboardFD ? serverSocket:keyboardFD;

    const char* menu = "Welcome to CSE radio station!\n"
    		"1.Receive information about a chosen station\n"
    		"2.Upload a new song\n"
    		"3.Change station\n"
    		"4.Exit\n"
    		"Pleas insert the number of your choice:";

    //insert the socket and the keyboard FD into set
    fd_set monitored;
    FD_ZERO(&monitored);
    FD_SET(serverSocket,&monitored);
    FD_SET(keyboardFD,&monitored);

	do{

		//monitor the socket and the keyboard
		puts("Playing awesome music..\n(Insert any input to reach the menu.)\n");
		selectVal = select(maxFD+1,&monitored,NULL,NULL,NULL);
		check_select(selectVal,"connection established.\n");

		//check which FD triggered the select() function
		input =-1;
		if(FD_ISSET(keyboardFD,&monitored))
			input = get_integer_from_user(menu,ASKSONG,EXIT);
		else
			{//act according to the message received
			int messageType;
				messageType = check_message("Got a message from server in connection established.\n");
				if(messageType == PERMITSONG )
					clean_exit("Got a permit message in connection established.\n");
				if(messageType == ANNOUNCE)
					clean_exit("Got an announce message in connection established.\n");
				continue;
			}

		//check which operation do next
		switch(input){
		case ASKSONG:
			ask_song();
			break;
		case UPSONG:
			upload_song();
			break;
		case CHANGE_STATION:
			ask_song();
			//currentStation = get_integer_from_user("Please enter the number of station you wish to listen to :",0,numOfStations);
			break;
		}
    }while(input != EXIT);

	//going offline after receiving the EXIT command
	clean_exit("Legit exit");
}
int get_integer_from_user(const char* message,int min, int max){
	int input;
	//clean buffer
	while(getchar() != '\n');
	do{
		puts(message);
		scanf("%d",&input);

		//check if input is legit
		if(input <= max && input >= min)
			break;
		printf("Invalid input.\nPlease enter a new number in the proper range (from %d to %d).\n",min,max);
	}while(TRUE);

	return input;
}
void get_NewStation_message(){
	NewStations newStations;
	int size = sizeof(newStations.newStationNumber);
	if ( read(serverSocket, &newStations.newStationNumber, size) < size)
		clean_exit("Failed reading new station message.\n");
    printf("Received a NewStation message.\n");

    //update the number of stations
    numOfStations = newStations.newStationNumber + 1;
}

int check_message(char* failMsg) {
	char messageType;
	//get the replyType field
	if (read(serverSocket, &messageType, sizeof(char)) < sizeof(char)) {
		clean_exit(failMsg);
	}
	//messageType = ntohs(messageType);
	//check what kind of message is it and continue accordenly
	switch (messageType) {
	case NEWSTATIONS:
		get_NewStation_message();
		break;
	case ANNOUNCE:
		break;
	case PERMITSONG:
		break;
	case WELCOME:
		printf("Received welcome at: ");
		clean_exit(failMsg);
		break;
	case INVALIDCOMMAND:
		get_invalidCommand_message();
		break;
	}
	return messageType;//return can be ANNOUNCE or NEWSTATIONS or PERMITSONG
}

void ask_song(){
	int size = sizeof(AskSong),input,selectVal;
	const char* message = "Please insert the number of the station you wish to receive information about:\n";
	//define the set for select
	fd_set monitored;
	FD_ZERO(&monitored);
	FD_SET(serverSocket,&monitored);

    //create the timeout struct
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = ASKSONG_TIMEOUT;

    INIT_ASKSONG(askSong);

    //get the number of the station from the user
	input = get_integer_from_user(message,0,numOfStations - 1);

	currentStation = input;
	//set the number of the station in the message
	/**************for some reason the server reverse the station number upon receive...***********/
	askSong.stationNumber = htons(input) ;

	//askSong.stationNumber = input;

	//send the ask song message
	if ( write(serverSocket, &askSong, size) < size )
		clean_exit("Failed sending the ask song message.\n");
	printf("Ask song message was sent.\n");

	//wait for a reply (or timeout)
	selectVal = select(serverSocket+1,&monitored,NULL,NULL,&timeout);
	check_select(selectVal,"Waiting for announce message.\n");

	//get a reply, announce song message
	Announce announce;
	int messageType;

	do{
		messageType = check_message("reading reply type after ask song message.\n");
		if(messageType == PERMITSONG){
			clean_exit("Got a permitSong message while waiting for announce message.\n");
		}
	}while(messageType != ANNOUNCE);

	if(read(serverSocket,&announce.songNameSize,sizeof(char))<sizeof(char)){
		clean_exit("Failed: reading song name size after ask song message.\n");
	}

	printf("received the first part of the announce message.\n");

	//allocate memory for song's name
	init_announce(&announce);

	//get the rest of the announce message
	if ( read(serverSocket, announce.songName, announce.songNameSize) < announce.songNameSize){
		free(announce.songName);
		clean_exit("Failed while reading: annonuce.songName.\n ");
	}
	printf("Announce message was received.\n");

	//print the answer to the user's query
	printf("Station number %d is playing the song: %s.\n",input,announce.songName);

	//free allocated memory
	free(announce.songName);
}
int upload_song(){
	char songName[MAX_LEN_SONG_NAME+1] = {0};

	//get the song's name from user
	do{
	songName[MAX_LEN_SONG_NAME] = 0;
	printf("Please enter the name of the song you wish to upload:\n");
	scanf("%s",songName);
	while(getchar() != '\n');
	if(songName[MAX_LEN_SONG_NAME] != 0)
	printf("Name is too long!\nPleas enter name upto %d chars",MAX_LEN_SONG_NAME);
	}while(songName[MAX_LEN_SONG_NAME] != 0);

	FILE* fp = fopen(songName,"r");
	if(!fp){
		puts("Failed: opening file to upload.\n");
		return 0;
	}

	//set upSong message
	UpSong upSong;
	upSong.commandType = UPSONG;
	upSong.songNameSize = strlen(songName);
	//get the song's size
	fseek(fp,NULL,SEEK_END);
	upSong.songSize = ftell(fp);
	fseek(fp,NULL,SEEK_SET);
	upSong.songName = songName;

	//check if song size is valid
	if(upSong.songSize > MAX_SONG_SIZE){
		puts("Song selected is too big.\n");
		fclose(fp);
		return 0;
	}
	if(upSong.songSize < MIN_SONG_SIZE){
		puts("Song selected is too small.\n");
		fclose(fp);
		return 0;
	}

	upSong.songSize = htonl(upSong.songSize);
	//prepare the set fd for timeout check
	fd_set monitored;
	FD_ZERO(&monitored);
	FD_SET(serverSocket,&monitored);

    //create the timeout struct
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = UP_TIMEOUTE;

    //send the upSong message
    int size =sizeof(upSong);
    if ( write(serverSocket, &upSong, size) < size )
    	clean_exit("failed while sending upsong message.\n");
    puts("UpSong message was sent.\n");

    //check for timeout
    int selectVal;
	selectVal = select(serverSocket+1,&monitored,NULL,NULL,&timeout);
	check_select(selectVal,"waiting for a permit message.\n");


	//get permit message from server
	INIT_PERMITSONG(permitSong);
	size = sizeof(permitSong.permit)+sizeof(permitSong.replyType);
	int messageType;
	do{
	messageType = check_message("waiting for perimit message.\n");
	if(messageType == ANNOUNCE){
		clean_exit("Got an announce message while waiting for permit message.\n");
	}

	}while(messageType != PERMITSONG);
	puts("Got a permit message.\n");

	if(!permitSong.permit){
		puts("Server is busy right now.\n Please try again later.\n");
		fclose(fp);
		return 0;
	}

	//uploading song
	upSong.songSize = ntohl(upSong.songSize);/*************for some reason server reverse songSize feiled**************/
	int leftToSend = upSong.songSize;
	char buffer[UPSONG_CHUNK] = {0};
	printf("Uploading song...\n");

	while(fread( buffer,sizeof(char), UPSONG_CHUNK/sizeof(char),fp) == UPSONG_CHUNK){

		//send chunk to server
		if ( write(serverSocket, &buffer, UPSONG_CHUNK) < UPSONG_CHUNK ){
			puts("Failed to send a chunk of data while uploading a song.\n");
			return 0;
		}
		usleep(RATE_CTRL);
		fflush(stdout);
		printf("\r %lu%% ",(ftell(fp)*100)/upSong.songSize);
		leftToSend -= UDP_CHUNK;
	}
	//send last chunk
	if ( write(serverSocket, &buffer, leftToSend) < leftToSend )
		clean_exit("Failed to send last chunk of data.\n");
	printf("\r %d%%\n",100);

	//set timeout and fd_set for new station massege
    timeout.tv_sec = NEWSTATION_TIMEOUT;
    timeout.tv_usec = 0;
	FD_ZERO(&monitored);
	FD_SET(serverSocket,&monitored);

	//waiting for new station message
	selectVal = select(serverSocket + 1 ,&monitored, NULL,NULL,&timeout);////max fd
	check_select(selectVal,"while waiting for new station message.\n");
/*
		//get the replyType field
		if (read(serverSocket, &messageType, sizeof(char)) < sizeof(char)) {
			clean_exit("waiting for a new station message\n");
		}
		messageType = htons(messageType);
		//check what kind of message is it and continue accordenly
		switch (messageType) {
		case NEWSTATIONS:
			get_NewStation_message();
			break;
		case ANNOUNCE:
			break;
		case PERMITSONG:
			break;
		case WELCOME:
			printf("Received welcome at: ");
			clean_exit("waiting for a new station message\n");
			break;
		case INVALIDCOMMAND:
			get_invalidCommand_message();
			break;
		}
		*/
	messageType = check_message("waiting for a new station message\n");
	if(messageType == PERMITSONG )
		clean_exit("Got a permit message after uploading song.\n");
	if(messageType == ANNOUNCE)
		clean_exit("Got an announce message after uploading song.\n");

	fclose(fp);

return 1;

}
void get_invalidCommand_message(){
	InvalidCommand invalidcmd;
	int size = sizeof(invalidcmd.replyStringSize);
	if(read(serverSocket,&invalidcmd.replyStringSize,size) < size)
		clean_exit("failed to read the size of the string in the invalid message,\n");
	init_InvalidCommand(&invalidcmd);
	scanf("Got an invalidCommand message: '%s'",invalidcmd.replyString);
	free(invalidcmd.replyString);
	clean_exit("");
}
void init_announce(Announce* message) {
	message->replyType = ANNOUNCE;
	message->songName = (char*)malloc(message->songNameSize * sizeof(char)+1);
	if(!message->songName)
		puts("malloc() in init_announce failed.");
}

void init_InvalidCommand(InvalidCommand* message) {
	message->replyType = INVALIDCOMMAND;
	message->replyString = (char*)malloc(message->replyStringSize * sizeof(char));
	if(!message->replyString)
		puts("malloc() in init_invalidCommand failed.");
}
