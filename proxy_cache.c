 ////////////////////////////////////////////////////////////////////////
// File Name	: proxy_cache.c					      //
// Date		: 2022/06/01					      //
// OS		: Ubuntu 16.04 LTS 64bits			      //
// Author	: Kim Jae Yoon					      //
// Student ID	: 2018202005					      //
//------------------------------------------------------------------- //
// Title : System Programming Assignment #3-2 (proxy server)	      //
// Description: This is Proxy server which connect with web browser   //
// 		If browser input url, server accept url 	      //
//		and check cache directory which HIT or MISS condition //
//		Send request msg and receive response msg	      //
//		Web Server & Browser display each result	      //
//		+ make semaphore to block other processes	      //
//		  during one process do the task		      //
//		  and make thread to write logfile		      //
////////////////////////////////////////////////////////////////////////

#include<stdio.h>
#include<string.h>
#include<dirent.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/wait.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<signal.h>
#include<stdlib.h>
#include<openssl/sha.h>
#include<time.h>
#include<pwd.h>
#include<fcntl.h>
#include<netdb.h>
#include<sys/ipc.h>
#include<sys/sem.h>
#include<pthread.h>

#define BUFFSIZE 1024
#define PORTNO 39999

///////////////////////////////////////////////////////////////////////
// handler							     //
// ================================================================= //
// Purpose: wait before specific child process end		     //
//	    receive child process's terminated status		     //
///////////////////////////////////////////////////////////////////////
static void handler(){
	pid_t pid;
	int status;
	while((pid = waitpid(-1, &status, WNOHANG)) > 0);
}

///////////////////////////////////////////////////////////////////////
// getHomeDir							     //
// ================================================================= //
// Input: char* ->home						     //
//								     //
// Output: home	- home directory				     //
// 								     //
// Purpose: get home directory location				     //
//	    (in my pc, /home/user/ is the home dir)		     //
///////////////////////////////////////////////////////////////////////
char *getHomeDir(char *home){
	struct passwd *usr_info = getpwuid(getuid());
	strcpy(home, usr_info->pw_dir);
	
	return home;
}

///////////////////////////////////////////////////////////////////////
// sha1_hash 							     //
// ================================================================= //
// Input: char* -> input_url(will hash)				     //
//	  char* -> hashed_url(after hashed)			     //
//								     //
// Output: char* - hashed_url(copy hashed_hex)			     //
//								     //
// Purpose: hasing url using SHA1 function			     //
//	    and copy to the return value 			     //
///////////////////////////////////////////////////////////////////////
char *sha1_hash(char *input_url, char *hashed_url){
	unsigned char hashed_160bits[20];
	char hashed_hex[41];
	int i;

	SHA1(input_url, strlen(input_url), hashed_160bits);
	
	for(i=0;i<sizeof(hashed_160bits);i++)
		sprintf(hashed_hex + i*2, "%02x", hashed_160bits[i]);
	
	strcpy(hashed_url, hashed_hex);

	return hashed_url;
}

//SIGINT
int start_process[3];
int runtime_process = 0, process_cnt = 0;
void sigint(){
	printf("\n");
	FILE *logfile;
	char home_url[BUFFSIZE];

	//check terminated time and disconnect
	time_t now;
	time(&now);
	struct tm *ltp_e_p;
	ltp_e_p = localtime(&now);
	int end_process[3] = {ltp_e_p->tm_hour,ltp_e_p->tm_min,ltp_e_p->tm_sec};
	runtime_process = end_process[0] - start_process[0];
	runtime_process = runtime_process*60 + end_process[1] - start_process[1];
	runtime_process = runtime_process*60 + end_process[2] - start_process[2];

	//save and go to the home dir location
	getHomeDir(home_url);
	chdir(home_url);
	chdir("logfile");
	logfile = fopen("logfile.txt","a");
	fprintf(logfile, "**SERVER** [Terminated] run time: %d sec. #sub process: %d\n", runtime_process, process_cnt);
	fclose(logfile);
	exit(0);
}

///////////////////////////////////////////////////////////////////////
// getIPAddr 							     //
// ================================================================= //
// Input: char* -> will be dotted IP address			     //
//								     //
// Output: char* - host address					     //
//								     //
// Purpose: change big endian IP address			     //
//	    to dotted IPv4 address				     //
///////////////////////////////////////////////////////////////////////
char *getIPAddr(char *addr){
	struct hostent* hent;
	char *haddr;
	int len = strlen(addr);
	
	if((hent = (struct hostent*)gethostbyname(addr)) != NULL)
		haddr=inet_ntoa(*((struct in_addr*)hent->h_addr_list[0]));
	return haddr;
}

///////////////////////////////////////////////////////////////////////
// p, v 							     //
// ================================================================= //
// Input: int - semaphore id					     //
//								     //
// Output: nothing(void function)				     //
//								     //
// Purpose: make semaphore to use numerous 			     //
//	    resources by limited processes			     //
///////////////////////////////////////////////////////////////////////
void p(int semid){
	struct sembuf p;
	p.sem_num =0;
	p.sem_op=-1;
	p.sem_flg = SEM_UNDO;
	if((semop(semid, &p, 1)) == -1){
		perror("p: semop failed");
		exit(1);
	}
}
void v(int semid){
	struct sembuf v;
	v.sem_num = 0;
	v.sem_op= 1;
	v.sem_flg = SEM_UNDO;
	if((semop(semid, &v, 1)) == -1){
		perror("v: semop failed");
		exit(1);
	}
}

pthread_t tid;
///////////////////////////////////////////////////////////////////////
// Hit 							  	     //
// ================================================================= //
// Input: char* use_url						     //
//								     //
// Output: nothing(void function)				     //
//								     //
// Purpose: in the critical zone, find hit or miss		     //
//	    If hit, write hit state into the logfile		     //
///////////////////////////////////////////////////////////////////////
void *Hit(void* use_url){
	printf("*PID# %d create the *TID# %d.\n", getpid(), tid);

	FILE *logfile;
	char home_url[BUFFSIZE];
	getHomeDir(home_url);
	chdir(home_url);

	time_t now;
	struct tm *ltp;
	time(&now);
	ltp = localtime(&now);

	char subdir[4], h_url[41];
	sha1_hash(use_url, h_url);
	strncpy(subdir, h_url, 3);
	subdir[3] = '\0';
	
	chdir("logfile");
	logfile = fopen("logfile.txt","a");
	chmod("logfile.txt",S_IRWXU|S_IRWXG|S_IRWXO);
	fprintf(logfile, "[Hit] ServerPID : %d | %s/%s-[%02d/%02d/%02d, %02d:%02d:%02d]\n", getpid(), subdir, &h_url[3],
		ltp->tm_year+1900, ltp->tm_mon+1, ltp->tm_mday, ltp->tm_hour, ltp->tm_min, ltp->tm_sec);
	fprintf(logfile, "[Hit] %s\n", use_url);	
	fclose(logfile);
	printf("*TID# %d is exited.\n", tid);
}
///////////////////////////////////////////////////////////////////////
// Miss							  	     //
// ================================================================= //
// Input: char* use_url						     //
//								     //
// Output: nothing(void function)				     //
//								     //
// Purpose: in the critical zone, find hit or miss		     //
//	    If miss, write miss state into the logfile		     //
///////////////////////////////////////////////////////////////////////
void *Miss(void* use_url){
	printf("*PID# %d create the *TID# %d.\n", getpid(), tid);

	FILE *logfile;
	char home_url[BUFFSIZE];
	getHomeDir(home_url);
	chdir(home_url);

	time_t now;
	struct tm *ltp;
	time(&now);
	ltp = localtime(&now);
	
	chdir(home_url);
	chdir("logfile");
	logfile = fopen("logfile.txt","a");
	chmod("logfile.txt",S_IRWXU|S_IRWXG|S_IRWXO);
	fprintf(logfile, "[Miss] ServerPID : %d | %s-[%02d/%02d/%02d, %02d:%02d:%02d]\n", getpid(), use_url,
		ltp->tm_year+1900, ltp->tm_mon+1, ltp->tm_mday, ltp->tm_hour, ltp->tm_min, ltp->tm_sec);
	fclose(logfile);
	printf("*TID# %d is exited.\n", tid);
}

int main(int argc, char *argv[]){
	FILE * logfile;
	struct dirent *pFile;
	DIR *pDir;

	struct sockaddr_in server_addr, client_addr, tmp_addr;
	int socket_fd, client_fd, tmp_fd;
	int len;
	char buf[BUFFSIZE];
	char home_url[BUFFSIZE];
	pid_t pid;

	//save and go to the home dir location
	getHomeDir(home_url);
	pDir = opendir(home_url);
	chdir(home_url);

	//if no cache dir, make new directory or else something similar job
	int find_cache = 0;
	int find_logfile = 0;
	int find_process = 0;

	//to check run time
	time_t now;
	struct tm *ltp_s_p, *ltp_e_p;
	
	//find logfile & cache dir in the home dir
	for(pFile=readdir(pDir);pFile;pFile=readdir(pDir)){		
		if(!strcmp(pFile->d_name,"logfile")){
			find_logfile=1;
			break;
		}
	}
	//rewind
	pDir = opendir(home_url);
	for(pFile=readdir(pDir);pFile;pFile=readdir(pDir)){	
		if(!strcmp(pFile->d_name,"cache")){	
			find_cache=1;
			break;
		}
	}

	//if logfile or cache dir is not in home dir, make with umask(000)	
	if(find_logfile == 0){
		umask(000);
		mkdir("logfile",S_IRWXU|S_IRWXG|S_IRWXO);
	}
	if(find_cache == 0){
		umask(000);
		mkdir("cache",S_IRWXU|S_IRWXG|S_IRWXO);
	}

	//make a socket to connect with clients
	if((socket_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0){
		printf("Server : Can't open stream socket\n");
		return 0;
	}

	//bind socket every time after disconnect with server(^c)
	int opt = 1;
	setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	
	//bind socket between server & client
	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(PORTNO);

	if(bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr))<0){
		printf("Server: Can't bind local address.\n");
		close(socket_fd);
		return 0;
	}

	//for client request connection
	listen(socket_fd, 5);
	signal(SIGCHLD, (void *) handler);
	signal(SIGINT, (void *) sigint);

	//check process' start time
	time(&now);
	ltp_s_p = localtime(&now);
	start_process[0] = ltp_s_p->tm_hour;
	start_process[1] = ltp_s_p->tm_min;
	start_process[2] = ltp_s_p->tm_sec;

	//accept client's connection and do the task
	while(1){
		struct in_addr inet_client_address;

		char buf[BUFFSIZE];
        
		char response_header[BUFFSIZE] = {0, };
		char response_message[BUFFSIZE] = {0, };

		char tmp[BUFFSIZE] = {0, };
		char method[20] = {0, };
		char url[BUFFSIZE] = {0, };
		char use_url[BUFFSIZE] = {0, };
		char host_url[BUFFSIZE] = {0, };
		char *tok = NULL;

		bzero((char *)&client_addr, sizeof(client_addr));
		len = sizeof(client_addr);
		client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &len);

		if(client_fd < 0){
			printf("Server: accept failed.\n");
			close(socket_fd);
			return 0;
		}

		inet_client_address.s_addr = client_addr.sin_addr.s_addr;

		int semid;
		union semun{
			int val;
			struct semid_ds *x;
			unsigned short int *arr;
		} arg;
		
		if((semid = semget((key_t)PORTNO,1,IPC_CREAT|0666)) == -1){
			perror("semget failed");
			exit(1);
		}
	
		arg.val=1;
		if((semctl(semid,0,SETVAL,arg))==-1){
			perror("semctl failed");
			exit(1);
		}
		
		//counting process
		process_cnt++;
		pid = fork();

		if(pid == -1){
			close(client_fd);
			close(socket_fd);
			continue;
		}

		if(pid == 0){
			//semaphore start
			printf("*PID# %d is waiting for the semaphore.\n", getpid());
			sleep(7);
			p(semid);
			printf("*PID# %d is in the critical zone.\n", getpid());
			sleep(10);
			read(client_fd, buf, BUFFSIZE);
			strcpy(tmp, buf);
			tok = strtok(tmp, " ");
			strcpy(method, tok);

			//check POST or GET, if not GET, continue
			if(strcmp(method, "GET") == 0){
				tok = strtok(NULL, " ");
				strcpy(url, tok);
				strcpy(use_url, url);
			}
			else
				break;

			//get rid of http:// url's part
			strcpy(use_url, &use_url[7]);
			for(int i=0;i<BUFFSIZE;i++){
				if(use_url[i] == '/'){
					strncpy(host_url, use_url, i);
					break;
				}
			}

			//hashing url
			char h_url[41];
			sha1_hash(use_url, h_url);

			//to find same hashed_url[0:2] dir
			int find_file = 0;

			//first three code sub-directory
			char subdir[4];
			for(int x=0;x<3;x++)
				subdir[x]=h_url[x];
			subdir[3] = '\0';

			
			//HIT or MISS ->to make file & dir in cache dir
			chdir(home_url);
			chdir("cache");
			pDir = opendir(".");
			for(pFile=readdir(pDir);pFile;pFile=readdir(pDir)){
				//HIT
				if(!strncmp(pFile->d_name, subdir, 3)){
					pDir = opendir(subdir);
					for(pFile=readdir(pDir);pFile;pFile=readdir(pDir)){	
						if(!strcmp(pFile->d_name, &h_url[3])){
							find_file = 1;
							int fd = open(&h_url[3], O_RDONLY);
							int len_out = 0;
							fflush(stdout);

							//read response message from the file
							while((len_out = read(fd, buf, sizeof(buf))) > 0){
								write(client_fd, buf, strlen(buf));
								bzero(buf, sizeof(buf));
							}

							int err = pthread_create(&tid, NULL, Hit, (void*)use_url);
							if(err != 0) {
								printf("pthread_create() eror.\n");
								return 0;
							}
							close(fd);
							close(client_fd);
						}
					}
				}
			}
				
			//MISS
			if(find_file == 0){
				umask(000);
				mkdir(subdir, S_IRWXU|S_IRWXG|S_IRWXO);
				chdir(subdir);
				int fd = creat(&h_url[3], 0777);

				char *host = getIPAddr(host_url);

				//make a socket to connect with tmp
				if((tmp_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0){
					printf("Server : Can't open stream socket\n");
					return -1;
				}
		
				//bind socket every time after disconnect with server
				int opt_ = 1;
				setsockopt(tmp_fd, SOL_SOCKET, SO_REUSEADDR, &opt_, sizeof(opt_));

				bzero((char*)&tmp_addr, sizeof(tmp_addr));
				tmp_addr.sin_family = AF_INET;
				tmp_addr.sin_addr.s_addr = inet_addr(host);
				tmp_addr.sin_port = htons(80);

				int len_out = sizeof(tmp_addr);
				
				if(connect(tmp_fd, (struct sockaddr*)&tmp_addr, sizeof(tmp_addr)) < 0){
					printf("Can't connect.\n");
					return -1;
				}

				write(tmp_fd, buf, sizeof(buf));
				bzero(buf, sizeof(buf));

				//read response message
				while((len_out = read(tmp_fd, buf, sizeof(buf))) > 0){
					write(client_fd, buf, strlen(buf));
					write(fd, buf, strlen(buf));
					bzero(buf, sizeof(buf));
				}
				int err = pthread_create(&tid, NULL, Miss, (void*)use_url);
				if(err != 0) {
					printf("pthread_create(0 eror.\n");
					return 0;
				}
				close(tmp_fd);
				close(client_fd);
			}
			void *tret;
			pthread_join(tid, &tret);

			//get out of critical zone
			v(semid);
			printf("*PID# %d exited the critical zone.\n", getpid());

			exit(0);
		}//end of pid == 0 (disconnected client)
		//wait before child process exit
	}
	close(socket_fd);

	return 0;
}
