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
#include <dirent.h>
#include <sys/stat.h>

#define MAX_CACHE_SIZE 5000000
#define MAX_OBJECT_SIZE 512000

#define QUAD_BUFF_SIZE 4096
#define DOUBLE_BUFF_SIZE 2048
#define DEFAULT_BUFF_SIZE 1024
#define HALF_BUFF_SIZE 512
#define QUARTER_BUFF_SIZE 256
#define QQUARTER_BUFF_SIZE 128
#define QQQUARTER_BUFF_SIZE 64

void INIT__PROXY__SERVER(int PORT_NUM);
void PARSE__HTTP__REQUEST(int WEB_BROWSER_SOCKET , char *HTTP_REQUEST);
char *GET_IP_FROM_HOST(char *HOST);
void INITIALIZE__CONNECTION__PROXYCLIENT__ORIGINSERVER(int WEB_BROWSER_SOCKET , char *PASED , char *IP , char *HOST, char *OBJECT);
void PROXY_LOG_WRITER(char *HEADER_LINE , char *IP , char *HOST , char *OBJECT , int TRS);
int SEARCH_CACHE(char *HOST , char *OBJECT , int WEB_BROWSER_SOCKET);
void HIT__CACHE__TRANSMISSION(char *FILENAME , int WEB_BROWSER_SOCKET);
int CACHE_VALIDATOR(int TRS);

int main( int argc , char*argv[]) {

    if(argc <2){
        printf("프록시 서버를 열기위한 파라미터가 충분하지 않습니다.\n");
        printf("사용법 : ./proxy 12345");
    }
    int PORT_NUM = atoi(argv[1]);
    INIT__PROXY__SERVER(PORT_NUM);
    return 0;
}

void INIT__PROXY__SERVER(int PORT_NUM){
    printf("프록시 서버 생성 및 구성 중.... \n\n\n");

    int WEB_BROWSER_SOCKET , PROXY_SERVER_SOCKET;
    int WEB_BROWSER_ADDR_SIZE;

    int T_FLAG = 1;
    int F_FLAG = 0;
    socklen_t T_SIZE = sizeof(T_FLAG);
    socklen_t F_SIZE = sizeof(F_FLAG);
    pid_t pid;

    struct sockaddr_in PROXY_SERVER_ADDR;
    struct sockaddr_in WEB_BROWSER_ADDR;

    PROXY_SERVER_SOCKET = socket(PF_INET, SOCK_STREAM , 0);
    WEB_BROWSER_SOCKET = socket(PF_INET, SOCK_STREAM , 0);

    if(access("PROXY_CACHE",F_OK) == 0){
        printf("사용하던 캐시 디렉토리 발견. 계속 이용합니다.\n");
    }
    else {
        printf("캐시 디렉토리가 존재하지 않습니다. 새로 생성합니다.\n");
        if (mkdir("PROXY_CACHE",0777) == -1){
            printf("캐시 디렉토리 생성에 실패하였습니다. \n");
            exit(1);
        }
        else{
            printf("캐시 기능을 위한 디렉토리 생성에 성공.\n");
        }
    }

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
                PARSE__HTTP__REQUEST(WEB_BROWSER_SOCKET ,WEB_BROWSER_HTTP_REQUEST);
                exit(0);
            }
            else{
                printf("GET , POST 메소드 이외의 요청은 처리하지 않습니다.\n");
                free(WEB_BROWSER_HTTP_REQUEST);
                exit(1);
            }

        }
        if(pid > 0){
            wait(0);


            struct dirent *DIR_STRUCT;
            struct stat *FILE_STAT;
            DIR *DIR_PTR;
            int FILE_SIZE = 0;
            int CURRENT_CACHE_SIZE = 0;

            char *CWD_BUFFER = (char *)malloc(DEFAULT_BUFF_SIZE);
            memset(CWD_BUFFER , 0 , DEFAULT_BUFF_SIZE);
            chdir("PROXY_CACHE");
            getcwd(CWD_BUFFER , DEFAULT_BUFF_SIZE);

            if((DIR_PTR = opendir(CWD_BUFFER)) == -1){
                perror("캐시 디렉토리 여는데에 실패 :");
                exit(1);
            }
            while ((DIR_STRUCT = readdir(DIR_PTR)) != NULL) {

                if ((strcmp(DIR_STRUCT->d_name, ".") && strcmp(DIR_STRUCT->d_name, "..")) == 1) {

                    if (lstat(DIR_STRUCT->d_name, FILE_STAT) == -1) {
                        perror("파일 정보 획득에 실패 :");
                        exit(1);
                    }
                    FILE_SIZE = FILE_STAT->st_size;
                    CURRENT_CACHE_SIZE = CURRENT_CACHE_SIZE + FILE_SIZE;
                }
            }

            printf("\n\n전체 캐시 용량 : %d(Bytes)\n",5000000);
            printf("사용 캐시 용량 : %d(Bytes)\n",CURRENT_CACHE_SIZE);
            printf("잔여 캐시 용량 : %d(Bytes)\n",(5000000-CURRENT_CACHE_SIZE));

            rewinddir(DIR_PTR);
            closedir(DIR_PTR);
            chdir("..");
            printf("요청에 대한 응답을 마쳤습니다. 다음 요청에 대기합니다.\n");
            close(WEB_BROWSER_SOCKET);
        }

    }

}
void PARSE__HTTP__REQUEST(int WEB_BROWSER_SOCKET , char * HTTP_REQUEST) {

    char *METHOD = (char *) malloc(QQQUARTER_BUFF_SIZE);
    char *OBJECT = (char *) malloc(QUARTER_BUFF_SIZE);
    char *HOST = (char *) malloc(QUARTER_BUFF_SIZE);
    char *HTTP = (char *) malloc(QQQUARTER_BUFF_SIZE);
    char *PASED = (char *) malloc(DOUBLE_BUFF_SIZE);
    char *REQUEST_COPY = (char *) malloc(DOUBLE_BUFF_SIZE);
    char *REQUEST_COPY2 = (char *) malloc(DOUBLE_BUFF_SIZE);
    char *REQUEST_COPY3 = (char *) malloc(DOUBLE_BUFF_SIZE);
    char *REQUEST_ONE_LINE = (char *) malloc(DEFAULT_BUFF_SIZE);
    char *USER_LINE = (char *) malloc(DEFAULT_BUFF_SIZE);
    char *AE_LINE = (char *) malloc(DEFAULT_BUFF_SIZE);
    char *IP;
    char *U_adr;
    char *AE_adr;


    memcpy(REQUEST_COPY, HTTP_REQUEST, DOUBLE_BUFF_SIZE);
    memcpy(REQUEST_COPY2, HTTP_REQUEST, DOUBLE_BUFF_SIZE);
    memcpy(REQUEST_COPY3, HTTP_REQUEST, DOUBLE_BUFF_SIZE);

    U_adr = strstr(REQUEST_COPY3, "User-Agent: ");
    AE_adr = strstr(REQUEST_COPY3, "Accept-Encoding: ");

    strcpy(USER_LINE, U_adr);
    strcpy(AE_LINE, AE_adr);

    METHOD = strtok(REQUEST_COPY, " ");
    //printf("메소드 : %s\n",METHOD);
    HTTP = strtok(NULL, "//");
    HOST = strtok(NULL, "/");
    //printf("호스트: %s\n",HOST);
    //printf("HTTP : %s\n" , HTTP);

    REQUEST_ONE_LINE = strtok(REQUEST_COPY2, " ");
    REQUEST_ONE_LINE = strtok(NULL, " ");

    for (int i = strlen(USER_LINE) - strlen(AE_LINE); i <= strlen(USER_LINE); i++) {
        USER_LINE[i] = 0;
    }

    if (!strcmp(HTTP, "http:")) {
        OBJECT = REQUEST_ONE_LINE + 7 + strlen(HOST);
//        printf("OBJECT : %s\n" , OBJECT);


    }
    if (!strcmp(HTTP, "https:")) {
        OBJECT = REQUEST_ONE_LINE + 8 + strlen(HOST);
//        printf("OBJECT : %s\n" , OBJECT);
    }

    sprintf(PASED, "%s %s HTTP/1.0\r\nHost: %s\r\n%sAccept-Encoding: gzip, deflate\r\nConnection: close\r\n\r\n",
            METHOD, OBJECT, HOST, USER_LINE);
    printf("HTTP/1.0으로 만들어진 리퀘스트는 다음과 같습니다.\n%s\n", PASED);

    printf("해당 요청이 캐시에 존재하는지 먼저 확인합니다.\n\n");

    int CACHE_STATE = 0;
    CACHE_STATE = SEARCH_CACHE(HOST, OBJECT , WEB_BROWSER_SOCKET);

    if(CACHE_STATE == 0){

        IP = GET_IP_FROM_HOST(HOST);
        printf("호스트를 통해 확보한 IP ADDRESS : %s\n", IP);
        INITIALIZE__CONNECTION__PROXYCLIENT__ORIGINSERVER(WEB_BROWSER_SOCKET, PASED, IP, HOST, OBJECT);

    }


}
void HIT__CACHE__TRANSMISSION(char *FILENAME , int WEB_BROWSER_SOCKET){
    if(chdir("PROXY_CACHE") == -1){
        perror("캐시 디렉토리로 이동에 실패 :");
        exit(1);
    }

    int FILE_FD = open( FILENAME , O_RDWR , 0777);
    if(FILE_FD == -1){
        perror("캐시파일을 여는데에 실패 :");
        exit(1);
    }
    lseek(FILE_FD , 0 , SEEK_SET);

    int READ_SIZE = 0;
    char *FILE_DATA = (char *)malloc(QUAD_BUFF_SIZE);
    memset(FILE_DATA , 0 , QUAD_BUFF_SIZE);

    while ((READ_SIZE = read( FILE_FD , FILE_DATA , QUAD_BUFF_SIZE)) > 0){
        write(WEB_BROWSER_SOCKET , FILE_DATA , strlen(FILE_DATA));
        memset(FILE_DATA , 0 , QUAD_BUFF_SIZE);
    }

    printf("캐쉬 HIT에 대한 데이터 전송 완료!\n");
    chdir("..");
}
int SEARCH_CACHE(char *HOST , char *OBJECT , int WEB_BROWSER_SOCKET){
    struct dirent *DIR_SRUCT;
    DIR *DIR_PTR;

    int DIR_SIZE = 0;
    int HIT_STATE = 0;
    char *FILENAME = (char *)malloc(QUARTER_BUFF_SIZE);
    memset(FILENAME , 0 , QUARTER_BUFF_SIZE);
    strcat(FILENAME,HOST);
    strcat(FILENAME,OBJECT);
    for (int i = 0; i < strlen(FILENAME); ++i) {
        if(FILENAME[i] == '/'){
            FILENAME[i] = '-';
        }
    }

    if((DIR_PTR = opendir("PROXY_CACHE")) == NULL){
        perror("캐시 디렉토리를 여는데에 실패하였습니다.");
        exit(1);
    };
    if(DIR_PTR != NULL){
        for(;;){
            DIR_SRUCT = readdir(DIR_PTR);
            if(DIR_SRUCT == NULL){
                break;
            }
            if(!strcmp(FILENAME,DIR_SRUCT->d_name)){
                printf("캐시 HIT 발생! 해당 요청은 캐시에서 전송합니다. %s에 대해.\n" , FILENAME);
                HIT__CACHE__TRANSMISSION(FILENAME,WEB_BROWSER_SOCKET);
                HIT_STATE = 1;
                break;
            }
        }
        rewinddir(DIR_PTR);
        closedir(DIR_PTR);
    }
//    free(FILENAME);
    return HIT_STATE;
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
    char *DATA_BUFFER = (char *)malloc(512000);
    memset(DATA_BUFFER , 0 , 512000);

    int READ_SIZE = 0;
    int WRITE_SIZE = 0;
    int TOTAL_READ_SIZE = 0;

    int DATA_STREAM_INDEX = 0;
    char *HEADER_LINE_PTR = (char *)malloc(2000);
    memset(HEADER_LINE_PTR , 0 , 2000);

    char *FILENAME = (char *)malloc(QUARTER_BUFF_SIZE);
    memset(FILENAME , 0 , QUARTER_BUFF_SIZE);
    strcat(FILENAME,HOST);
    strcat(FILENAME,OBJECT);

    for (int i = 0; i < strlen(FILENAME); ++i) {
        if(FILENAME[i] == '/'){
            FILENAME[i] = '-';
        }
    }


    while ((READ_SIZE = read( PROXY_CLIENT_SOCKET , RECEIVE_BUFFER , QUAD_BUFF_SIZE)) > 0){
        TOTAL_READ_SIZE = TOTAL_READ_SIZE + READ_SIZE;
        if(DATA_STREAM_INDEX == 0){
            strncpy( HEADER_LINE_PTR , RECEIVE_BUFFER , 1024);
        }

       strncat(DATA_BUFFER , RECEIVE_BUFFER , strlen(RECEIVE_BUFFER));
       write(WEB_BROWSER_SOCKET , RECEIVE_BUFFER , READ_SIZE);
       printf("응답 내용은 다음과 같습니다 \n%s\n",RECEIVE_BUFFER);
       memset(RECEIVE_BUFFER , 0 , QUAD_BUFF_SIZE);
       DATA_STREAM_INDEX++;
    }

    /*
     * LRU 알고리즘 적용.
     * 1. 캐시 디렉토리를 먼저 조사한다.
     * 2. 디렉토리내 파일 용량이 5MB를 초과하지 않는지 검사한다.
     * 3. 파일에 버퍼 내용을 기록하기 전에 현재 디렉토리 사이즈 + 버퍼 내용의 길이가 5MB를 초과한다면
     * 가작 Access 타임이 오래된 파일부터 삭제 -> 용량 비교 -> 삭제를 거듭한다.
     * 4. 용량이 충분히 확보되면 파일에 내용 기록.
     */


    PROXY_LOG_WRITER(HEADER_LINE_PTR , IP , HOST , OBJECT , TOTAL_READ_SIZE);



    int CACHE_STATE = 0;
    if(chdir("PROXY_CACHE") == -1){
        perror("디렉토리 이동에 실패 :");
        exit(1);
    }

    int FILE_FD = open(FILENAME , O_RDWR | O_CREAT , 0777);
    if(FILE_FD == -1){
        perror("캐시파일 생성 에러 :");
        exit(1);
    }

    if(TOTAL_READ_SIZE < MAX_OBJECT_SIZE){

    CACHE_STATE = CACHE_VALIDATOR(TOTAL_READ_SIZE);

        if(CACHE_STATE == 1){
            write(FILE_FD , DATA_BUFFER , strlen(DATA_BUFFER));
            close(FILE_FD);
            chdir("..");
        }
    }
    else{
        close(FILE_FD);
        unlink(FILENAME);
        chdir("..");
    }

    shutdown(PROXY_CLIENT_SOCKET , SHUT_RDWR);
    close(PROXY_CLIENT_SOCKET);
    free(PASED);
}
int CACHE_VALIDATOR(int TRS) {

    struct dirent *DIR_STRUCT;
    struct stat *FILE_STAT;

    int CACHE_STATE = 0;
    int CURRENT_CACHE_SIZE = 0;
    int FILE_SIZE = 0;

    DIR *DIR_PTR;

    char *MIN_ACC_FILE = (char *) malloc(QUARTER_BUFF_SIZE);
    memset(MIN_ACC_FILE, 0, QUARTER_BUFF_SIZE);

    char *CWD_BUFFER = (char *) malloc(DEFAULT_BUFF_SIZE);
    memset(CWD_BUFFER, 0, DEFAULT_BUFF_SIZE);

    getcwd(CWD_BUFFER, DEFAULT_BUFF_SIZE);

    if((DIR_PTR = opendir(CWD_BUFFER)) == -1){
        perror("캐시 디렉토리 여는데에 실패 :");
        exit(1);
    }
    while ((DIR_STRUCT = readdir(DIR_PTR)) != NULL) {

        if ((strcmp(DIR_STRUCT->d_name, ".") && strcmp(DIR_STRUCT->d_name, "..")) == 1) {

            if (lstat(DIR_STRUCT->d_name, FILE_STAT) == -1) {
                perror("파일 정보 획득에 실패 :");
                exit(1);
            }
            FILE_SIZE = FILE_STAT->st_size;
            CURRENT_CACHE_SIZE = CURRENT_CACHE_SIZE + FILE_SIZE;
        }
    }

    rewinddir(DIR_PTR);

    if ((CURRENT_CACHE_SIZE + TRS) <= 5000000) {
        CACHE_STATE = 1;
    }
    if (CURRENT_CACHE_SIZE + TRS > 5000000) {

        if ((DIR_PTR = opendir(CWD_BUFFER)) == NULL) {
            perror("캐시 디렉토리를 여는데에 실패하였습니다. :");
            exit(1);
        }
        while (CURRENT_CACHE_SIZE + TRS >= 5000000) {
        // 캐쉬사이즈가 확보될 때까지 루프를 반복한다.
            int Inode_Pointer = 0;
            time_t MIN_ACC_TIME;
            while ((DIR_STRUCT = readdir(DIR_PTR)) != NULL) {

                if ((strcmp(DIR_STRUCT->d_name, ".") && strcmp(DIR_STRUCT->d_name, "..")) == 1) {

                    if (lstat(DIR_STRUCT->d_name, FILE_STAT) == -1) {
                        perror("파일 정보 획득에 실패 :");
                        exit(1);
                    }
                    if (Inode_Pointer == 0) {
                        MIN_ACC_TIME = FILE_STAT->st_atime;
                        Inode_Pointer++;
                    }
                    if (Inode_Pointer > 0) {
                        if (MIN_ACC_TIME > FILE_STAT->st_atime) {
                            MIN_ACC_TIME = FILE_STAT->st_atime;
                            Inode_Pointer++;
                        }

                    }
                }
            }
            rewinddir(DIR_PTR);
            // 제일 늦게 엑세스한 파일의 시간을 확인하고
            // 그 엑세스 시간과 일치하는 파일을 지워나간다.

            while ((DIR_STRUCT = readdir(DIR_PTR)) != NULL) {

                if ((strcmp(DIR_STRUCT->d_name, ".") && strcmp(DIR_STRUCT->d_name, "..")) == 1) {
                        if (lstat(DIR_STRUCT->d_name, FILE_STAT) == -1) {
                            perror("파일 정보 획득에 실패 :");
                            exit(1);
                        }
                        if (FILE_STAT->st_atime == MIN_ACC_TIME) {
                            printf("가장 최근에 참조되지 않은 데이터 부터 삭제합니다.\n");
                            unlink(DIR_STRUCT->d_name);
                        }
                    }
                }
            rewinddir(DIR_PTR);
            }
            CACHE_STATE = 1;
    }
    closedir(DIR_PTR);
    return CACHE_STATE;
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

