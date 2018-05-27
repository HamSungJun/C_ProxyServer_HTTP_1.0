/*
 * Description : HTTP/1.0 버전의 fork() 프로세스 모델을 이용한 웹 프록시 서버 구현.
 * Author : 한양대학교 컴퓨터 공학과 2013041007 함성준
 * Lecture : Computer Network
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <netdb.h>
#include <wait.h>

#define QUAD_BUFF_SIZE 4096
#define DOUBLE_BUFF_SIZE 2048
#define DEFAULT_BUFF_SIZE 1024
#define HALF_BUFF_SIZE 512
#define QUARTER_BUFF_SIZE 256
#define QQUARTER_BUFF_SIZE 128
#define QQQUARTER_BUFF_SIZE 64

void INIT__PROXY__SERVER(int PORT_NUM);
void PARSE__HTTP__REQUEST(int WEB_BROWSER_SOCKET , char *HTTP_REQUEST , int FIFO_FD);
char *GET_IP_FROM_HOST(char *HOST);
void INITIALIZE__CONNECTION__PROXYCLIENT__ORIGINSERVER(int WEB_BROWSER_SOCKET , char *PASED , char *IP , char *HOST, char *OBJECT);
void PROXY_LOG_WRITER(char *HEADER_LINE , char *IP , char *HOST , char *OBJECT , int TRS);
int main( int argc , char*argv[]) {

    if(argc <2){
        printf("프록시 서버를 열기위한 파라미터가 충분하지 않습니다.\n");
        printf("사용법 : ./proxy 12345");
    }

    INIT__PROXY__SERVER(atoi(argv[1]));
    return 0;
}

void INIT__PROXY__SERVER(int PORT_NUM){
    printf("프록시 서버 생성 및 구성 중.... \n\n\n");

    int WEB_BROWSER_SOCKET , PROXY_SERVER_SOCKET;
    int WEB_BROWSER_ADDR_SIZE;
    int FIFO_FD;
    int T_FLAG = 1;
    int F_FLAG = 0;
    socklen_t T_SIZE = sizeof(T_FLAG);
    socklen_t F_SIZE = sizeof(F_FLAG);
    pid_t pid;

    struct sockaddr_in PROXY_SERVER_ADDR;
    struct sockaddr_in WEB_BROWSER_ADDR;

    PROXY_SERVER_SOCKET = socket(PF_INET, SOCK_STREAM , 0);
    WEB_BROWSER_SOCKET = socket(PF_INET, SOCK_STREAM , 0);

    if(PROXY_SERVER_SOCKET == -1){
        perror("프록시 서버 소켓 생성에 실패하였습니다 :");
        exit(1);
    }
    else{
        printf("프록시 서버 소켓 디스크립터 생성에 성공. \n");
    }
    if(WEB_BROWSER_SOCKET == -1){
        perror("웹 브라우저 소켓 생성에 실패하였습니다. :");
        exit(1);
    }
    else{
        printf("웹 브라우저 소켓 디스크립터 생성에 성공. \n");
    }

    memset( &PROXY_SERVER_ADDR , 0 , sizeof(PROXY_SERVER_ADDR));
    PROXY_SERVER_ADDR.sin_family = AF_INET;
    PROXY_SERVER_ADDR.sin_port = htons(PORT_NUM);
    PROXY_SERVER_ADDR.sin_addr.s_addr = htonl(INADDR_ANY);

    if(setsockopt( PROXY_SERVER_SOCKET , SOL_SOCKET , SO_REUSEADDR , &T_FLAG , T_SIZE) == -1){
        perror("프록시 서버 소켓 옵션 설정에 실패하였습니다. :");
        exit(1);
    }
    else{
        printf("프록시 서버 소켓 옵션 설정에 성공.\n");
    }

    if(bind( PROXY_SERVER_SOCKET , (struct sockaddr *)&PROXY_SERVER_ADDR , sizeof(PROXY_SERVER_ADDR)) == -1){
        perror("프록시 서버 소켓과의 바인딩에 실패하였습니다. :");
        exit(1);
    }
    else{
        printf("프록시 서버 디스크립터와의 바인딩에 성공.\n");
    }

    if(listen(PROXY_SERVER_SOCKET , 5) == -1){
        perror("프록시 서버와의 연결 요청에 실패하였습니다. :");
        exit(1);
    }
    else{
        printf("프록시 서버와의 연결 요청에 성공.\n\n\n");
        printf("프록시 서버 생성 및 구성 완료! 서버 로직을 실행합니다. \n\n");
    }

    while (1){

        WEB_BROWSER_ADDR_SIZE = sizeof(WEB_BROWSER_ADDR);
        WEB_BROWSER_SOCKET = accept( PROXY_SERVER_SOCKET , (struct sockaddr*)&WEB_BROWSER_ADDR , &WEB_BROWSER_ADDR_SIZE);

        pid = fork();

        if(pid == -1){
            printf("요청 처리 프로세스 생성에 실패하였습니다. \n");
            exit(1);
        }
        else if(pid == 0){
            /* 자식프로세스
             * 1. HTTP 요청을 확인한다.
             * 2. 요청 METHOD가 'GET'일 경우 HTTP/1.0 정책으로 PARSE.
             * 3. 추려낸 OBJECT명과 HOST를 확인하여 캐쉬에 내용이 존재하는지 확인한다.
             * 4. 캐쉬에 존재하는 내용이 없는 경우 추려낸 HOST로 IP주소를 확인하여 해당 서버와 연결한다.
             * 5. 서버로부터 해당하는 OBJECT를 받아오고 로그를 작성한다.
             * 6. 캐쉬구조에 호스트와 오브젝트에 대한 캐쉬 내용을 작성한다.
             */

            if(WEB_BROWSER_SOCKET == -1){
                perror("프록시 서버와 웹 브라우저의 연결 실패.");
                exit(1);
            }
            char *WEB_BROWSER_HTTP_REQUEST = (char *)malloc(DOUBLE_BUFF_SIZE);
            memset(WEB_BROWSER_HTTP_REQUEST , 0 , DOUBLE_BUFF_SIZE);
            if(read(WEB_BROWSER_SOCKET,WEB_BROWSER_HTTP_REQUEST,DOUBLE_BUFF_SIZE ) == -1){
                perror("웹 브라우저의 HTTP 요청 수신에 실패하였습니다.");
                exit(1);
            }

            printf("새로운 웹 브라우저 접속 요청을 수락합니다. \n");
            printf("\n\n<---------- 웹 브라우저의 HTTP 요청 내용 ---------->\n\n");
            printf("%s\n",WEB_BROWSER_HTTP_REQUEST);

            if(strstr(WEB_BROWSER_HTTP_REQUEST,"GET") || strstr(WEB_BROWSER_HTTP_REQUEST,"POST")){
                PARSE__HTTP__REQUEST(WEB_BROWSER_SOCKET ,WEB_BROWSER_HTTP_REQUEST , FIFO_FD);
                exit(0);
            }
            else{
                printf("GET , POST 메소드 이외의 요청은 처리하지 않습니다.");
                free(WEB_BROWSER_HTTP_REQUEST);
                exit(1);
            }

        }
        if(pid > 0){
            wait(0);
            printf("요청에 대한 응답을 마쳤습니다. 다음 요청에 대기합니다.\n");
            close(WEB_BROWSER_SOCKET);
        }

    }

}
void PARSE__HTTP__REQUEST(int WEB_BROWSER_SOCKET , char * HTTP_REQUEST , int FIFO_FD){

    char *METHOD = (char *)malloc(QQQUARTER_BUFF_SIZE);
    char *OBJECT = (char *)malloc(QUARTER_BUFF_SIZE);
    char *HOST = (char *)malloc(QUARTER_BUFF_SIZE);
    char *HTTP = (char *)malloc(QQQUARTER_BUFF_SIZE);
    char *PASED = (char *)malloc(DOUBLE_BUFF_SIZE);
    char *REQUEST_COPY = (char *)malloc(DOUBLE_BUFF_SIZE);
    char *REQUEST_COPY2 = (char *)malloc(DOUBLE_BUFF_SIZE);
    char *REQUEST_COPY3 = (char *)malloc(DOUBLE_BUFF_SIZE);
    char *REQUEST_ONE_LINE = (char *)malloc(DEFAULT_BUFF_SIZE);
    char *USER_LINE = (char *)malloc(DEFAULT_BUFF_SIZE);
    char *AE_LINE = (char *)malloc(DEFAULT_BUFF_SIZE);
    char *IP;
    char *U_adr;
    char *AE_adr;


    memcpy( REQUEST_COPY , HTTP_REQUEST , DOUBLE_BUFF_SIZE);
    memcpy( REQUEST_COPY2 , HTTP_REQUEST , DOUBLE_BUFF_SIZE);
    memcpy( REQUEST_COPY3 , HTTP_REQUEST , DOUBLE_BUFF_SIZE);

    U_adr = strstr(REQUEST_COPY3 , "User-Agent: ");
    AE_adr = strstr(REQUEST_COPY3 , "Accept-Encoding: ");

    strcpy(USER_LINE , U_adr);
    strcpy(AE_LINE , AE_adr);

    METHOD = strtok( REQUEST_COPY , " ");
    //printf("메소드 : %s\n",METHOD);
    HTTP = strtok( NULL , "//");
    HOST = strtok( NULL , "/");
    //printf("호스트: %s\n",HOST);
    //printf("HTTP : %s\n" , HTTP);

    REQUEST_ONE_LINE = strtok( REQUEST_COPY2 , " ");
    REQUEST_ONE_LINE = strtok( NULL , " ");

    for(int i = strlen(USER_LINE) - strlen(AE_LINE) ; i <= strlen(USER_LINE) ; i++){
        USER_LINE[i] = 0;
    }

    if(!strcmp(HTTP , "http:")){
        OBJECT = REQUEST_ONE_LINE+7+strlen(HOST);
//        printf("OBJECT : %s\n" , OBJECT);


    }
    if(!strcmp(HTTP , "https:")){
        OBJECT = REQUEST_ONE_LINE+8+strlen(HOST);
//        printf("OBJECT : %s\n" , OBJECT);
    }

    sprintf( PASED ,"%s %s HTTP/1.0\r\nHost: %s\r\n%sAccept-Encoding: gzip, deflate\r\nConnection: close\r\n\r\n",METHOD,OBJECT,HOST,USER_LINE);
    printf("HTTP/1.0으로 만들어진 리퀘스트는 다음과 같습니다.\n%s\n",PASED);



    IP = GET_IP_FROM_HOST(HOST);
    printf("호스트를 통해 확보한 IP ADDRESS : %s\n" , IP);
    INITIALIZE__CONNECTION__PROXYCLIENT__ORIGINSERVER(WEB_BROWSER_SOCKET ,  PASED , IP , HOST , OBJECT);

}
char *GET_IP_FROM_HOST(char *HOST){

    struct hostent *HOST_STRUCT;
    struct in_addr **addr_list;
    int i;
    char *IP;
    if((HOST_STRUCT = gethostbyname(HOST)) == NULL){
        herror("hethostbyname");
        exit(1);
    }

    addr_list = (struct in_addr **)HOST_STRUCT->h_addr_list;

    for(i = 0 ; addr_list[i] != NULL; i++){
        IP = inet_ntoa(*addr_list[i]);
    }


    return IP;

}
void INITIALIZE__CONNECTION__PROXYCLIENT__ORIGINSERVER(int WEB_BROWSER_SOCKET , char *PASED , char *IP , char *HOST , char *OBJECT){

    int PROXY_CLIENT_SOCKET = socket(PF_INET, SOCK_STREAM, 0);
    if (PROXY_CLIENT_SOCKET == -1) {
        perror("프록시 클라이언트 소켓 생성에 실패하였습니다. :");
        exit(1);
    }

    struct sockaddr_in ORIGIN_SERVER_ADDR;

    memset(&ORIGIN_SERVER_ADDR , 0 ,sizeof(ORIGIN_SERVER_ADDR));
    ORIGIN_SERVER_ADDR.sin_family = AF_INET;
    ORIGIN_SERVER_ADDR.sin_port = htons(80);
    ORIGIN_SERVER_ADDR.sin_addr.s_addr = inet_addr(IP);

    if(connect(PROXY_CLIENT_SOCKET , (struct sockaddr*)&ORIGIN_SERVER_ADDR , sizeof(ORIGIN_SERVER_ADDR))){
        printf("프록시 클라이언트와 오리진 서버와의 연결에 실패하였습니다. \n");
        exit(1);
    }

    if(write(PROXY_CLIENT_SOCKET , PASED , DOUBLE_BUFF_SIZE) == -1){
        perror("오리진 서버쪽으로 리퀘스트 전달에 실패하였습니다. ");
        exit(1);
    }

    char *RECEIVE_BUFFER = (char *)malloc(QUAD_BUFF_SIZE);
    memset(RECEIVE_BUFFER , 0 , QUAD_BUFF_SIZE);

    int READ_SIZE = 0;
    int WRITE_SIZE = 0;
    int TOTAL_READ_SIZE = 0;

    int DATA_STREAM_INDEX = 0;
    char *HEADER_LINE_PTR = (char *)malloc(2000);
    memset(HEADER_LINE_PTR , 0 , 2000);

    while ((READ_SIZE = read( PROXY_CLIENT_SOCKET , RECEIVE_BUFFER , QUAD_BUFF_SIZE)) > 0){
        TOTAL_READ_SIZE = TOTAL_READ_SIZE + READ_SIZE;
        if(DATA_STREAM_INDEX == 0){
            strncpy( HEADER_LINE_PTR , RECEIVE_BUFFER , 1024);
        }
        write(WEB_BROWSER_SOCKET , RECEIVE_BUFFER , READ_SIZE);
        printf("응답 내용은 다음과 같습니다 \n%s\n",RECEIVE_BUFFER);
        memset(RECEIVE_BUFFER , 0 , QUAD_BUFF_SIZE);
        DATA_STREAM_INDEX++;
    }

    PROXY_LOG_WRITER(HEADER_LINE_PTR , IP , HOST , OBJECT , TOTAL_READ_SIZE);
    shutdown(PROXY_CLIENT_SOCKET , SHUT_RDWR);
    close(PROXY_CLIENT_SOCKET);

}

void PROXY_LOG_WRITER(char *HEADER_LINE , char *IP , char *HOST , char *OBJECT , int TRS){
    // 1.parse headerline
    // 2.Date : ??? EST : IP http:// HOST | OBJECT | TRS
    // 3.close FD
    int FILE_PROXYLOG_FD;
    if((FILE_PROXYLOG_FD = open( "PROXYLOG" , O_RDWR | O_CREAT | O_SYNC | O_APPEND , 0777))  == -1){
        perror("로그용 파일 여는데에 실패.");
        exit(1);
    }

    if(access("PROXYLOG",F_OK)  == -1){
        perror("프록시 로그용 파일이 존재하지 않습니다.");
        exit(1);
    }

    char *LOG_LINE_PTR = (char *)malloc(2000);
    memset(LOG_LINE_PTR , 0 , 2000);
    char *HEADER_LINE_COPY_PTR = (char *)malloc(2000);
    memset(HEADER_LINE_COPY_PTR , 0 , 2000);

    strncpy(HEADER_LINE_COPY_PTR,HEADER_LINE,strlen(HEADER_LINE));

    char *DATE_ADR = strstr(HEADER_LINE_COPY_PTR , "Date: ");
    char *DATE_LINE = strtok(DATE_ADR , "\r\n");

    if(DATE_LINE == NULL){
        sprintf( LOG_LINE_PTR , "NO DATE EST : %s http://%s%s => %d\n" , IP , HOST , OBJECT , TRS);
    }
    else{
        sprintf( LOG_LINE_PTR , "%s EST : %s http://%s%s => %d\n" , DATE_LINE , IP , HOST , OBJECT , TRS);
    }

    if(write( FILE_PROXYLOG_FD , LOG_LINE_PTR , strlen(LOG_LINE_PTR)) == -1){
        perror("로그용 파일에 작성중 오류 발생. ");
        exit(1);
    }

    printf("\n\n로그를 작성중입니다 ... %s 완료!\n\n",LOG_LINE_PTR);
    close(FILE_PROXYLOG_FD);
    free(LOG_LINE_PTR);
    free(HEADER_LINE_COPY_PTR);

}


