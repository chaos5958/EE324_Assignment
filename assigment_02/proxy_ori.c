#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>

#define MAXDATASIZE 200000
#define REQMSGSIZE 200000
#define BACKLOG 10

/**************************************************************************
  Extra work
  =Files used for caching (which will be created in run-time)
  1. cache_map: save all cache information (struct cache)
                except response message from server
  2. file**: each file which has this format store one response message from server	
  =Implementation
  1. If there is request from client, first, find same request and host in "cache_map"
  2. If there is exactly same copy, check timeout condition
  3. If it satifies timeout condition, open file("file**") which file name is written in "cache_map"
  4. Read "file**" and send it to client
***************************************************************************/


void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void *get_in_addr(struct sockaddr *sa)
{
    if(sa->sa_family == AF_INET){
	return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


/* STRUCT: host_port 
   used in is_valid_host function to return both host and port value  
*/
struct host_port {
    char *host;
    char *port;
};

/* STRUCT: cache
   used for implementing cache
   =parameter
   1. host: host in the request message from client
   2. request: request in the request message from clinet
   3. filename: filename for the file which contains repsonse for the request message
   4. time: time which is parsed in the response message from server by caculate_time() function 
*/
struct cache {
    char *host;
    char *request; 
    char* filename;
    char* time;
};

/* =============Details of functions are described in each definition================*/
static void send_bad_request (int sockfd);
static struct host_port *is_valid_host (char *host);
static bool is_valid_version (char *ver);

static void cache_init (struct cache *elem);
static void cache_free (struct cache *elem);
static char* retrieve_from_cache (struct cache* elem, time_t curr_time, char *timeout);
static int calculate_time (char *date);
static int string_to_weekday (char *);
static int string_to_month (char *);
/* ==================================================================================*/

int main(int argc, char *argv[])
{
    /* Variable Description
       1. sockfd: sock for listening from client
       2. new_fd: sock for recieving from client
       3. hints, serveinfo, p, their_addr, sin_size, sa, yes, s, rv => from Beejs
       4. numbytes: return value of rev() meaning rec data length
       5. buf: buf for receiving data from client
       6. PORT_NUM: port number got from input argument parsing
       7. param_opt: return value of getopt() for input argument parsing
       8. is_parent: check for child process or parant process 
       9. req_msg: request from the client
       10. file_index: index for creating diffent file names in each child process
       11. is_cached_empty: indicate "cache_map" file is empty or not
       12. timeout: timeout value for caching
       13. curr_time: time when request message is come and create a child process
     */

    int sockfd, new_fd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    int numbytes;
    char buf[MAXDATASIZE];
    char buf_[MAXDATASIZE];
    char req_msg[REQMSGSIZE];
    char *PORT_NUM = NULL;
    int child_iter = 0;

    int param_opt;
    int is_p = 0;
    pid_t is_parent = 1;

    static int file_index = 0;
    bool is_cached_empty = true;
    char *timeout;
    time_t curr_time;
    
    // 1. check proper number of arguments typed in command
    if(argc < 2 || argc >3){
	fprintf(stderr, "usage: argument number incorrect\n");
	exit(1);
    }
    
    if (argc == 2)
	timeout = "100000";
    else 
	timeout = argv[2];

    PORT_NUM = argv[1];



    // 2. Check PORT_NUM is appropriate number
    if(atoi(PORT_NUM) < 1024 || atoi(PORT_NUM) > 65535){
	fprintf(stderr, "usage: unavaliable port && use 1024~65535\n");
	exit(1);
    }
    PORT_NUM = argv[1];

    // 3. Code from Beejs server.c
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if((rv = getaddrinfo(NULL, PORT_NUM, &hints, &servinfo)) != 0)
    {
	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
	return 1;
    }

    for(p=servinfo; p != NULL; p = p->ai_next)
    {
	if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))==-1){
	    perror("server: socket");
	    continue;
	}

	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
	    perror("estsockpot");
	}

	if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
	    close(sockfd);
	    perror("server: bind");
	    continue;
	}

	break;
    }

    if (p == NULL){
	fprintf(stderr, "server: failed to bind\n");
	return 2;
    }

    freeaddrinfo(servinfo);

    if(listen(sockfd, BACKLOG) == -1){
	perror("listen");
	exit(1);
    }

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if(sigaction(SIGCHLD, &sa, NULL) == -1) {
	perror("sigaction");
	exit(1);
    }

    is_parent = 1;

    memset (buf, 0, sizeof buf);
    memset (req_msg, 0, sizeof req_msg);

    FILE* fp = fopen ("is_cached_empty", "w");
    fwrite ("true\n", sizeof (char), 6, fp);
    fclose (fp);

    while(1)
    {
	// 4. accept() and fork || only running in parent process
	if(is_parent != 0){
	    sin_size = sizeof their_addr;
	    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
	    if (new_fd == -1){
		perror("accept");
		close(sockfd);
		exit(1);
	    }
	    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr*)&their_addr), s, sizeof s);
	    fprintf(stdout, "server: got connection from %s\n", s);

	    fp = fopen ("is_cached_empty", "r");
	    fread (buf, sizeof (char), sizeof buf, fp);
	    if (strcmp (buf, "true\n") != 0)
		is_cached_empty = false;
	
	    memset (buf, 0, sizeof buf);

	    file_index ++;
	    curr_time = time (NULL);
	    is_parent = fork();

	}

	// 5. close the new_fd of parant process because we don't need it || only running in parent process 
	if(is_parent != 0){
	    close(new_fd);
	}


	// 6. receive request from the client || only running in the child process
	if(is_parent == 0){

	    /* variable 
	       1. px_sockfd: socket to communicate with the server
	       2. i: number of call function strtok 
	       3. host: starting address of the Host header line
	       4. ver: starting address of the Request line
	       5. proxy_cache: save all infomation about the request message from client and the corresponding response message from server 
	          It is used for caching
	       */

	    static int px_sockfd; 	    
	    static int i = 0;
	    static char *host;
	    static char *ver; 
	    static struct cache *proxy_cache;
	    // when the proxy receive message from the client, paste into req_msg
    	    if ((numbytes == recv (new_fd, buf, MAXDATASIZE, 0)) == -1)
		{
		    perror ("proxy: receive");
		    exit (-1);
		}

	    strncpy (&req_msg[strlen (req_msg)], buf, sizeof req_msg - strlen (req_msg));

	    memset (buf, 0, sizeof buf);

	    // when the proxy find \r\n\r\n in the req_msg 	    
	    // parse the req_msg for appropriate response
	    if (strstr (req_msg, "\r\n\r\n") != NULL)
	    {
		int off_t = 0;
		char *ptr_; 
		proxy_cache = (struct cache*) malloc (sizeof (struct cache));

		//parse and get ver
		ptr_ = strtok (req_msg, "\r\n"); 
		ver = ptr_;
		strncpy (buf + off_t, ptr_, sizeof buf - strlen (buf));
		off_t += strlen (ptr_);
		strncpy (buf + off_t, "\r\n", sizeof buf - strlen (buf));
		off_t += 2;
		i++;

		while (ptr_ = strtok (NULL, "\r\n"))
		{ 

		    //parse and get host
		    //check validity of version
		    if (i == 1)
		    {			
			host = ptr_;
			proxy_cache->request = (char *) malloc (sizeof (char) * (strlen (ver)+2));
			strcpy (proxy_cache->request, ver);
			strcat (proxy_cache->request, "\n");
			strncpy (buf + off_t, ptr_, sizeof buf - strlen (buf));
			off_t += strlen (ptr_);
			strncpy (buf + off_t, "\r\n", sizeof buf - strlen (buf));
			off_t += 2;

			if (is_valid_version (ver) == -1)
			{
			    perror ("proxy: invalid ver");
			    send_bad_request (new_fd);
			    cache_free (proxy_cache);
			    close (new_fd);
			    exit (-1);
			}


		    }

		    //parse
		    //check validity of host
		    //connect to the server if host is valid
		    else if (i == 2)			
		    {
			proxy_cache->host = (char *) malloc (sizeof (char) * (strlen (host) + 2));
			strcpy (proxy_cache->host, host);
			strcat (proxy_cache->host, "\n");
			strncpy (buf + off_t, ptr_, sizeof buf - strlen (buf));
			off_t += strlen (ptr_);
			strncpy (buf + off_t, "\r\n", sizeof buf - strlen (buf));
			off_t += 2;

			struct host_port *hp = is_valid_host (host);
			if (hp->host == NULL || hp->port == NULL)		    
			{
			    perror ("proxy: not valid host or port");
			    send_bad_request (new_fd);
			    cache_free (proxy_cache);
			    close (new_fd);
			    free (hp);
			    exit (-1);
			}
			else
			{
			    if((rv = getaddrinfo(hp->host, hp->port, &hints, &servinfo)) != 0){
				fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));				
				send_bad_request (new_fd);
				close (new_fd);
				cache_free (proxy_cache);
				free (hp);
				exit (-1);
			    }
			    free(hp);

			    for (p = servinfo; p != NULL; p = p->ai_next){
				if ((px_sockfd = socket (p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
				    perror ("proxy| client: socket");
				    continue;
				}

				if (connect (px_sockfd, p->ai_addr, p->ai_addrlen) == -1){
				    close (px_sockfd);
				    perror ("pxoxy| clinet: connect");

				    if (p == p->ai_next)
					break;
				    continue;
				}
				break;
			    }

			    if (p == NULL){
				fprintf(stderr, "proxy| clinet: failed to connect\n");
				send_bad_request (new_fd);
				cache_free (proxy_cache);
				close (new_fd);
				exit (-1);
			    }

			    inet_ntop (p->ai_family, get_in_addr ((struct sockaddr *)p->ai_addr), s, sizeof s);
			    printf ("proxy| client: connecting to %s\n", s);
			    freeaddrinfo (servinfo);	   		     
			}    			
		    }
		    //parse 
		    else{
			strncpy (buf + off_t, ptr_, sizeof buf - strlen (buf));
			off_t += strlen (ptr_);
			strncpy (buf + off_t, "\r\n", sizeof buf - strlen (buf));
			off_t += 2;
		    }

		    i++;

		}
	
		//i == 1 means there is no HOST header line, therefore, send bad request message to client
		if (i == 1)
			{
			    perror ("proxy: not valid host or port");
			    send_bad_request (new_fd);
			    cache_free (proxy_cache);
			    close (new_fd);
			    exit (-1);
			}
		
		//same as above if (i == 2)
		//this is the case for (strtok (NULL, " ") == NULL) when i == 2
		if (i == 2)			
		    {
			proxy_cache->host = (char *) malloc (sizeof (char) * (strlen (host) + 2));
			strcpy (proxy_cache->host, host);
			strcat (proxy_cache->host, "\n");

			struct host_port *hp = is_valid_host (host);
			if (hp->host == NULL || hp->port == NULL)		    
			{
			    perror ("proxy: not valid host or port");
			    send_bad_request (new_fd);
			    cache_free (proxy_cache);
			    close (new_fd);
			    free (hp);
			    exit (-1);
			}
			else
			{
			    if((rv = getaddrinfo(hp->host, hp->port, &hints, &servinfo)) != 0){
				fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
				send_bad_request (new_fd);
				cache_free (proxy_cache);
				close (new_fd);
				free (hp);
				exit (-1);
			    }
			    free (hp);

			    for (p = servinfo; p != NULL; p = p->ai_next){
				if ((px_sockfd = socket (p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
				    perror ("proxy| client: socket");
				    continue;
				}

				if (connect (px_sockfd, p->ai_addr, p->ai_addrlen) == -1){
				    close (px_sockfd);
				    perror ("pxoxy| clinet: connect");

				    if (p == p->ai_next)
					break;
				    continue;
				}
				break;
			    }

			    if (p == NULL){
			        perror("proxy| clinet: failed to connect\n");
				send_bad_request (new_fd);
				cache_free (proxy_cache);
				close (new_fd);
				exit (-1);
			    }

			    inet_ntop (p->ai_family, get_in_addr ((struct sockaddr *)p->ai_addr), s, sizeof s);
			    printf ("proxy| client: connecting to %s\n", s);
			    freeaddrinfo (servinfo);	   		     
			}    			
		    }

		strncpy (buf + off_t, "\r\n", sizeof buf - strlen (buf));
		off_t += 2;

		//If there is valid cached copy for the request message, return it and terminate child process
		if (is_cached_empty == false)
		{
		    char *ptr;
		    if ((ptr = retrieve_from_cache(proxy_cache, curr_time, timeout)) != NULL)
		    {	
			memset (buf, 0, sizeof buf);
			ptr = strtok (ptr, "\n");
			fp = fopen (ptr, "r");
			fread (buf, sizeof (char), sizeof buf, fp);
			if ((send (new_fd, buf, sizeof buf, 0) == -1))
			{
			    perror ("proxy| send");
			    exit (-1);
			}
			free (ptr);
			return 0;
		    }
		    free (ptr);
		}

		//Create the unique filename for saving the response message from server 
		char filename[10];
		sprintf (filename, "file%d", file_index);
		proxy_cache->filename = (char *)malloc (
			sizeof (char) * (strlen (filename) + 2));
		strcpy (proxy_cache->filename, filename);
		strcat (proxy_cache->filename, "\n");

	        //7. send request to the server 	
		if ((send (px_sockfd, buf, sizeof buf, 0) == -1))
		{
		    perror ("proxy| send");
		    cache_free (proxy_cache);
		    exit (-1);
		}

		memset (req_msg, 0, sizeof req_msg);
		while (1)
		{
		    //8. recv response from the server
		    memset (buf, 0, sizeof buf);

		    if((numbytes = recv(px_sockfd, buf, MAXDATASIZE, 0)) == -1){
			perror("proxy| recv");
			cache_free (proxy_cache);
			exit(1);
		    }

		    strcat (req_msg, buf);

		    if (numbytes == 0)
			break;

		    //9. send the response back to the client
		    if ((send (new_fd, buf, strlen (buf) + 1, 0) == -1))
		    {
			perror ("proxy| send");
			exit (-1);
		    }
		}

		
		//save the reponse message from server for caching
		FILE *map = fopen (filename, "w");
		fwrite (req_msg, sizeof (char), sizeof req_msg, map);
		fclose (map);
		
	
		//save time value of struct cacahe 
		memset (buf, 0, sizeof buf);
		char *date = strstr (req_msg, "Date: ");
		date = strtok (date, "\n");
		sprintf (buf, "%d", calculate_time (date));
		strcat (buf, "\n");
		proxy_cache->time = (char *)malloc (sizeof (char)*(strlen (buf) + 1));
		strcpy (proxy_cache->time, buf);

	
		//save the current request and response message information in "cache_map" for caching
		//(=save current proxy_cache in "cache_map")
		memset (buf, 0, sizeof buf);
		if (is_cached_empty == false)
		{
		    FILE *map = fopen ("cache_map", "r");
		    fseek (map, 0, SEEK_SET);
		    fread (buf, sizeof (char), sizeof buf, map);
		    fclose (map);
		}

		strcat (buf, proxy_cache->request);
		strcat (buf, proxy_cache->host);
		strcat (buf, proxy_cache->time);
		strcat (buf, proxy_cache->filename);

		FILE *map_ = fopen ("cache_map", "w");
		fseek (map_, 0, SEEK_SET);	
		fwrite (buf, sizeof (char), sizeof buf, map_);
		fclose (map_);

		if (is_cached_empty == true)
		{
		    map_ = fopen ("is_cached_empty", "w");
		    fwrite ("false\n", sizeof (char), 7, map_);
		    fclose (map_);
		}

		cache_free (proxy_cache);
		free (proxy_cache);
		close (new_fd);
		return 0;	
		
	    }
	}
    }

    close (sockfd);
    return 0;
}

/* FUNCTION is_valid_host
= parmeter
1. host: host headline in the request message from the clinet
= functionality
Check the host headline has appropriate syntax
If it is invalid, return host_port which one of element is NULL
*/

static struct host_port *is_valid_host (char *host)
{
    struct host_port* host_header = (struct host_port *) malloc (sizeof (struct host_port));
    
    host_header->host = NULL;
    host_header->port = "80";

    int iter = 0;
    char *ptr; 
    char *ptr_;

    ptr = strtok (host, " ");
    if (strcmp (ptr, "Host:") != 0)
	goto done;

    ptr = strtok (NULL, " ");  
    if (ptr == NULL)
	goto done;

    if (strtok (NULL, " "))	 	  
	goto done;

    ptr_ = strtok (ptr, ":");
    host_header->host = ptr_;

    ptr_ = strtok (NULL, ":");
    if (ptr_ == NULL)
	goto done;

    if (strtok (NULL, " ")){
	host_header->port = NULL;
	goto done;
    }
    else{
	if (atoi (ptr_) == 0)
	{
	    host_header->port = NULL;
	    goto done;
	}
	else
	    host_header->port = ptr_;
    }

done:
    return host_header; 
} 

/* FUNCTION: is_valid_version
   =parameter 
   1. ver: request line from the client
   =functionality
   Check the version in the request line is appropriate (=1.0)
*/

static bool is_valid_version (char *ver)
{
    char *ptr;
    char *http_ver;
    int i = 0;

    ptr = strtok (ver, " ");
    i++;

    while ( ptr = strtok (NULL, " "))
    {
	if (i == 2)
	    http_ver = ptr;

	i++;
    }

    if (i != 3 || http_ver == NULL){
	return false; 
    }

    else{	
	if (strcmp (http_ver, "HTTP/1.0") != 0)
	    return false;
	else 
	    return true;
    }
}

/* FUNCTION: send_bad_request
   =parameter
   1. sockfd: socket descriptor for communicating with the client
   =functionality
   send 400 Bad Request error message to the client
*/ 
static void send_bad_request (int sockfd)
{
    char *ret_status = "HTTP/1.0 400 Bad Request\r\n";
    char *week[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    char *month[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", 
			"Oct", "Nov", "Dec"};
    char *send_msg = (char *)malloc (100 * sizeof (char));
    memset (send_msg, 0, sizeof send_msg);

    time_t curr;
    time (&curr);
    struct tm *curr_tm = gmtime (&curr);
   
     
    int numbytes = sprintf (send_msg, "%sDate: %s, %02d %s %02d %02d:%02d:%02d GMT\r\n\r\n", ret_status, 
		week[curr_tm->tm_wday], curr_tm->tm_mday, month[curr_tm->tm_mon],
	       curr_tm->tm_year + 1900, curr_tm->tm_hour, curr_tm->tm_min, curr_tm->tm_sec);
    
    if ((send (sockfd, send_msg, numbytes, 0) == -1))
    {
	perror ("proxy| send");
	exit (-1);
    }

    free (send_msg);

}

/* FUNCTION: cache_init
   =parameter
   1. elem: cache which we want initialization
   =functionality
   initialize each element of struct cache
*/
static void cache_init (struct cache *elem)
{
    elem->host = NULL;
    elem->request = NULL;
    elem->filename = NULL;
    elem->time = NULL;
}

/* FUNCTION: cache_free
   =parmeter
   1. elem: cache which we want to delete it
   =functionality
   free each element of struct cache
*/

static void cache_free (struct cache *elem)
{
    free (elem->host);
    free (elem->request);
    free (elem->filename);
    free (elem->time);

}

/* FUNCTION: retreive_from_cache
   =parameter
   1. elem: struct cache which have information about current requesti message from the client
   2. curr_time: time when the current child_process is made 
    		 (= time when this request sent to the proxy)
   3. timeout: timeout value for caching
   =functionality
   Compare elem with each cache in "cache_map" which have all caches 
   If there is same cache in "cache_map", check timeout condition
   return cache corresponding if elem satisfies timeout condition
   for other cases return NULL
*/
static char* retrieve_from_cache (struct cache* elem, time_t curr_time, char *timeout)
{
    char *buf = (char *)malloc (sizeof (char) * 100);
    memset (buf, 0, 100);
    FILE *fp = fopen ("cache_map", "r");
    int i = 0;
    bool is_same_req = false;
    bool is_same = false;
    curr_time = curr_time + 40;

    while (fgets (buf, 100 , fp))
    {
	i++;
	if (strlen (buf) == 0)
	    break;

	if (i%4 == 0)
	    continue;

	if (i%4 == 1){
	    if (strcmp (buf, elem->request) == 0){
		is_same_req = true;
		continue;
	    }
	}
	
	if (i%4 == 2){
	    if (is_same_req == false)
		continue;
	    else{
		if (strcmp (buf, elem->host) == 0)
		    is_same = true;
		else
		    is_same_req = false;

		
	    }
    	}

	if (i%4 == 3){
	    if (is_same == false)
		continue;
	    else{
		if (atoi(buf) + atoi(timeout) > curr_time)
		{
		   memset (buf, 0, sizeof buf); 
		   fgets (buf, 100, fp);
		   return buf;
		}
		else
		{		    
		    is_same_req = false;
		    is_same = false;
		}
	    }
	}

    }

    return NULL;
}

/* FUNCTION: cacluate_time 
   =parameter 
   1. date: date header line from the server
   =functionality
   parse date and caculate time value by time() function
   return time value
 */ 
static int calculate_time (char *date)
{
    struct tm tm_;
    int sec;
    int min;
    int hour;
    int mday;
    int mon;
    int year;
    int wday;
    int yday;
    

    char *wday_ptr;
    char *ptr;
    char *ptr_ = NULL;
    int i = 0;

    ptr = strtok (date, " ");

    while (ptr = strtok (NULL, " "))
    {
	switch (i)
	{
	    case 1: wday_ptr = ptr_;
		    break;

	    case 2: mday = atoi (ptr_);
		    break;

	    case 3: mon = string_to_month (ptr_);
		    break;

	    case 4: year = atoi (ptr_) - 1900;
		    break;

	    case 5: ptr_ = strtok (ptr_, ":");
		    hour = atoi (ptr_);
		    ptr_ = strtok (NULL, ":");
		    min = atoi (ptr_);
		    ptr_ = strtok (NULL, ":");
		    sec = atoi (ptr_);
		    break;
	}

	if (i == 5)
	    break;
	
	i++;
	ptr_ = ptr;
    }
    tm_.tm_year = year;
    tm_.tm_mon = mon;
    tm_.tm_mday = mday;
    tm_.tm_hour = hour;
    tm_.tm_min = min;
    tm_.tm_sec = sec;
    tm_.tm_isdst = 0;

    setenv("TZ", "GMT", 1);
    time_t ti = mktime (&tm_);
    struct tm *tm_data = gmtime (&ti);
    
    return mktime (&tm_);
}

/* FUNCTION: string_to_weekday
   =parameter
   1. ptr: string which indicates weekday
   =functionality
   return appropriate int value for ptr
*/
static int string_to_weekday (char *ptr)
{

    if (strcmp (ptr, "Sun") == 0)
	return 0;
    if (strcmp (ptr, "Mon") == 0)
	return 1;
    if (strcmp (ptr, "Tue") == 0)
	return 2;
    if (strcmp (ptr, "Wed") == 0)
	return 3;
    if (strcmp (ptr, "Thu") == 0)
	return 4;
    if (strcmp (ptr, "Fri") == 0)
	return 5;
    if (strcmp (ptr, "Sat") == 0)
	return 6;

    return -1;
}

/* FUNCTION: string_to_month
   =parameter
   1. ptr: string which indicates month
   =functionality
   return appropriate int value for ptr
*/
static int string_to_month (char *ptr)
{
    if (strcmp (ptr, "Jan") == 0)
	return 0;
    if (strcmp (ptr, "Feb") == 0)
	return 1;
    if (strcmp (ptr, "Mar") == 0)
	return 2;
    if (strcmp (ptr, "Apr") == 0)
	return 3;
    if (strcmp (ptr, "May") == 0)
	return 4;
    if (strcmp (ptr, "Jun") == 0)
	return 5;
    if (strcmp (ptr, "Jul") == 0)
	return 6;
    if (strcmp (ptr, "Aug") == 0)
	return 7;
    if (strcmp (ptr, "Sep") == 0)
	return 8;
    if (strcmp (ptr, "Oct") == 0)
	return 9;
    if (strcmp (ptr, "Nov") == 0)
	return 10;
    if (strcmp (ptr, "Dec") == 0)
	return 11;

    return -1;
}

