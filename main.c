/*
 * Description : HTTP/1.0 버전의 fork() 프로세스 모델을 이용한 웹 프록시 서버 구현.
 * Author : 한양대학교 컴퓨터 공학과 2013041007 함성준
 * Lecture : Computer Network
 */

#include <sys/types.h> // 필요한 헤더 파일을 인클루드 합니다.
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

#define MAX_CACHE_SIZE 5000000 // 캐시 사이즈 한도와 오브젝트 사이즈 한도를 설정합니다.
#define MAX_OBJECT_SIZE 512000

#define QUAD_BUFF_SIZE 4096 // 버퍼로 활용하기 위한 사이즈를 설정합니다.
#define DOUBLE_BUFF_SIZE 2048
#define DEFAULT_BUFF_SIZE 1024
#define HALF_BUFF_SIZE 512
#define QUARTER_BUFF_SIZE 256
#define QQUARTER_BUFF_SIZE 128
#define QQQUARTER_BUFF_SIZE 64

void INIT__PROXY__SERVER(int PORT_NUM); // 프록시 서버 로직을 담당하는 함수입니다.
void PARSE__HTTP__REQUEST(int WEB_BROWSER_SOCKET , char *HTTP_REQUEST); // HTTP 요청을 파싱하는 함수입니다.
char *GET_IP_FROM_HOST(char *HOST); // 호스트로 IP주소를 얻어오는 함수입니다.
void INITIALIZE__CONNECTION__PROXYCLIENT__ORIGINSERVER(int WEB_BROWSER_SOCKET , char *PASED , char *IP , char *HOST, char *OBJECT); // 오리진 서버와 연결하여 데이터를 내려받는 함수입니다.
void PROXY_LOG_WRITER(char *HEADER_LINE , char *IP , char *HOST , char *OBJECT , int TRS); // 내려받은 데이터에 대해서 로그를 작성하는 함수입니다.
int SEARCH_CACHE(char *HOST , char *OBJECT , int WEB_BROWSER_SOCKET); // 요청 내용이 캐쉬에 존재하는지 확인하는 함수입니다.
void HIT__CACHE__TRANSMISSION(char *FILENAME , int WEB_BROWSER_SOCKET); // 요청내용에 부합하는 캐쉬 내용을 전송하는 함수입니다.
int CACHE_VALIDATOR(int TRS); // 내려받은 데이터를 캐시에 넣는 조건이 만족하는지 확인하는 함수입니다.

int main( int argc , char*argv[]) {

    if(argc < 2){ // 프로그램 실행을 위한 예외처리입니다.
        printf("프록시 서버를 열기위한 파라미터가 충분하지 않습니다.\n");
        printf("사용법 : ./proxy 12345");
    }
    int PORT_NUM = atoi(argv[1]); // 커맨드 상의 유저입력은 스트링이므로 정수형으로 변환합니다.
    INIT__PROXY__SERVER(PORT_NUM);
    return 0;
}

void INIT__PROXY__SERVER(int PORT_NUM){
    printf("프록시 서버 생성 및 구성 중.... \n\n\n");

    int WEB_BROWSER_SOCKET , PROXY_SERVER_SOCKET; // 소켓으로 활용하기 위한 FD를 선언합니다.
    int WEB_BROWSER_ADDR_SIZE;

    int T_FLAG = 1; // 소켓 옵션 지정을 위한 변수를 선언하고 할당합니다.
    int F_FLAG = 0;
    socklen_t T_SIZE = sizeof(T_FLAG);
    socklen_t F_SIZE = sizeof(F_FLAG);
    pid_t pid;

    struct sockaddr_in PROXY_SERVER_ADDR; // 소켓 FD와 바인딩할 주소 구조체를 선언합니다.
    struct sockaddr_in WEB_BROWSER_ADDR;

    PROXY_SERVER_SOCKET = socket(PF_INET, SOCK_STREAM , 0); // 소켓 디스크립터를 생성합니다.
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

    memset( &PROXY_SERVER_ADDR , 0 , sizeof(PROXY_SERVER_ADDR)); // 프록시 서버 구조체를 IPv4 , 입력한 포트번호 , Connect와 동시에 바인드 되도록 설정합니다.
    PROXY_SERVER_ADDR.sin_family = AF_INET;
    PROXY_SERVER_ADDR.sin_port = htons(PORT_NUM);
    PROXY_SERVER_ADDR.sin_addr.s_addr = htonl(INADDR_ANY);

    if(setsockopt( PROXY_SERVER_SOCKET , SOL_SOCKET , SO_REUSEADDR , &T_FLAG , T_SIZE) == -1){ // 프록시 서버 소켓이 주소를 재사용 할 수 있도록 설정합니다.
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
        WEB_BROWSER_SOCKET = accept( PROXY_SERVER_SOCKET , (struct sockaddr*)&WEB_BROWSER_ADDR , &WEB_BROWSER_ADDR_SIZE); // 서버가 웹 클라이언트로부터의 요청을 수락하도록 대기시킵니다.

        pid = fork(); // 새로운 요청 수락이 될 때마다 새로운 프로세스를 생성합니다.

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
            char *WEB_BROWSER_HTTP_REQUEST = (char *)malloc(DOUBLE_BUFF_SIZE); // 클라이언트의 HTTP REQUEST를 저장하기 위한 버퍼입니다.
            memset(WEB_BROWSER_HTTP_REQUEST , 0 , DOUBLE_BUFF_SIZE);
            if(read(WEB_BROWSER_SOCKET,WEB_BROWSER_HTTP_REQUEST,DOUBLE_BUFF_SIZE ) == -1){  // 클라이언트의 HTTP 요청을 읽어 버퍼에 저장합니다.
                perror("웹 브라우저의 HTTP 요청 수신에 실패하였습니다.");
                exit(1);
            }

            printf("새로운 웹 브라우저 접속 요청을 수락합니다. \n");
            printf("\n\n<---------- 웹 브라우저의 HTTP 요청 내용 ---------->\n\n");
            printf("%s\n",WEB_BROWSER_HTTP_REQUEST);

            if(strstr(WEB_BROWSER_HTTP_REQUEST,"GET") || strstr(WEB_BROWSER_HTTP_REQUEST,"POST")){ // 요청 메소드가 GET 혹은 POST 이면
                PARSE__HTTP__REQUEST(WEB_BROWSER_SOCKET ,WEB_BROWSER_HTTP_REQUEST); // HTTP요청을 파싱하여 다음 절차를 진행합니다.
                exit(0);
            }
            else{
                printf("GET , POST 메소드 이외의 요청은 처리하지 않습니다.\n"); // 메소드가 그 외의 것이면 파싱하지 않고 종료합니다.
                free(WEB_BROWSER_HTTP_REQUEST);
                exit(1);
            }

        }
        if(pid > 0){
            wait(0); // 자식프로세스의 정상종료를 기다립니다.
            shutdown(WEB_BROWSER_SOCKET,SHUT_RDWR);
            close(WEB_BROWSER_SOCKET); // 웹 브라우저 소켓을 닫습니다.
            printf("요청에 대한 응답을 마쳤습니다. 다음 요청에 대기합니다.\n");

        }

    }

}
void PARSE__HTTP__REQUEST(int WEB_BROWSER_SOCKET , char * HTTP_REQUEST) {
    // HTTP 요청을 파싱하기 위한 버퍼를 선언합니다.
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

    // 요청을 복사합니다.
    memcpy(REQUEST_COPY, HTTP_REQUEST, DOUBLE_BUFF_SIZE);
    memcpy(REQUEST_COPY2, HTTP_REQUEST, DOUBLE_BUFF_SIZE);
    memcpy(REQUEST_COPY3, HTTP_REQUEST, DOUBLE_BUFF_SIZE);

    // 계속 이용할 REQUEST 라인은 잡아둡니다.
    U_adr = strstr(REQUEST_COPY3, "User-Agent: ");
    AE_adr = strstr(REQUEST_COPY3, "Accept-Encoding: ");

    // 잡아둔 라인을 버퍼에 복사합니다.
    strcpy(USER_LINE, U_adr);
    strcpy(AE_LINE, AE_adr);


    METHOD = strtok(REQUEST_COPY, " "); //메소드를 토크나이즈를 통해서 획득합니다.
    //printf("메소드 : %s\n",METHOD);
    HTTP = strtok(NULL, "//"); // http:// , https:// 구분을 위해서 토크나이즈 합니다.
    HOST = strtok(NULL, "/"); // 호스트 획득을 위해서 토크나이즈 합니다.
    //printf("호스트: %s\n",HOST);
    //printf("HTTP : %s\n" , HTTP);

    REQUEST_ONE_LINE = strtok(REQUEST_COPY2, " ");
    REQUEST_ONE_LINE = strtok(NULL, " ");

    for (int i = strlen(USER_LINE) - strlen(AE_LINE); i <= strlen(USER_LINE); i++) { // 기존 헤더의 필요부분만 남기기 위해 버퍼 내용을 재설정합니다.
        USER_LINE[i] = 0;
    }

    if (!strcmp(HTTP, "http:")) { // 요청이 http:// 이면 오브젝트는 해당 주소부터 시작입니다.
        OBJECT = REQUEST_ONE_LINE + 7 + strlen(HOST);
//        printf("OBJECT : %s\n" , OBJECT);


    }
    if (!strcmp(HTTP, "https:")) { // 요청이 https:// 이면 오브젝트는 해당 주소부터 시작입니다.
        OBJECT = REQUEST_ONE_LINE + 8 + strlen(HOST);
//        printf("OBJECT : %s\n" , OBJECT);
    }

    sprintf(PASED, "%s %s HTTP/1.0\r\nHost: %s\r\n%sAccept-Encoding: gzip, deflate\r\nConnection: close\r\n\r\n",
            METHOD, OBJECT, HOST, USER_LINE); // 새로 파싱된 헤더를 버퍼에 저장합니다.
    printf("HTTP/1.0으로 만들어진 리퀘스트는 다음과 같습니다.\n%s\n", PASED);

    printf("해당 요청이 캐시에 존재하는지 먼저 확인합니다.\n\n");

    int CACHE_STATE = 0;
    CACHE_STATE = SEARCH_CACHE(HOST, OBJECT , WEB_BROWSER_SOCKET); // 요청을 보내기 전에 추출된 요소들로 캐시에 해당 내용이 이미 존재하는지 확인합니다.

    if(CACHE_STATE == 0){ // 캐시에 해당 내용이 존재하지 않는다면 오리진 서버와의 연결하여 데이터를 내려받습니다.

        IP = GET_IP_FROM_HOST(HOST); // 호스트를 통해서 IP주소를 얻어냅니다.
        printf("호스트를 통해 확보한 IP ADDRESS : %s\n", IP);
        INITIALIZE__CONNECTION__PROXYCLIENT__ORIGINSERVER(WEB_BROWSER_SOCKET, PASED, IP, HOST, OBJECT); // 서버로부터 데이터를 내려받고 로그를 작성하기 위한 함수를 실행합니다.

    }


}
void HIT__CACHE__TRANSMISSION(char *FILENAME , int WEB_BROWSER_SOCKET){
    if(chdir("PROXY_CACHE") == -1){ // 디렉토리 캐시로 이동합니다.
        perror("캐시 디렉토리로 이동에 실패 :");
        exit(1);
    }

    int FILESIZE;
    size_t READSIZE;
    FILE *FP = fopen(FILENAME , "rb");
    if(FP == NULL){
        perror("캐시파일을 여는데에 실패 :");
        exit(1);
    }


    fseek(FP,0,SEEK_END);
    FILESIZE = ftell(FP);
    fseek(FP , 0 , SEEK_SET);

    char *FILE_DATA = (char *)malloc(FILESIZE+1);
    memset(FILE_DATA , '\0' , FILESIZE+1);

    while (!feof(FP)){ // 파일의 데이터를 읽어서 버퍼에 저장합니다.

        READSIZE = fread(FILE_DATA , 1 , FILESIZE , FP);
        write(WEB_BROWSER_SOCKET , FILE_DATA , READSIZE); // 버퍼에 저장된 내용을 웹 브라우저로 보냅니다.
        memset(FILE_DATA , '\0' , FILESIZE+1); // 버퍼를 다시 초기화합니다.

    }

    printf("캐쉬 HIT에 대한 데이터 전송 완료!\n");
    fclose(FP);
    chdir(".."); // 기존 작업 디렉토리로 복귀합니다.
}
int SEARCH_CACHE(char *HOST , char *OBJECT , int WEB_BROWSER_SOCKET){
    struct dirent *DIR_SRUCT;
    DIR *DIR_PTR;

    int DIR_SIZE = 0;
    int HIT_STATE = 0;
    char *FILENAME = (char *)malloc(QUARTER_BUFF_SIZE); // 캐쉬 검색을 위한 파일 이름을 설정합니다. 디렉토리를 캐시로 이용할 경우 '/'를 허용하지 않기 때문에 '/' -> '-'로 설정하여 저장됩니다.
    memset(FILENAME , 0 , QUARTER_BUFF_SIZE);
    strcat(FILENAME,HOST);
    strcat(FILENAME,OBJECT);
    for (int i = 0; i < strlen(FILENAME); ++i) {
        if(FILENAME[i] == '/'){
            FILENAME[i] = '-';
        }
    }

    if((DIR_PTR = opendir("PROXY_CACHE")) == NULL){ // 캐시 디렉토리를 엽니다.
        perror("캐시 디렉토리를 여는데에 실패하였습니다.");
        exit(1);
    }
    if(DIR_PTR != NULL){ // 디렉토리 내부를 조사하면서 브라우저가 요청한 호스트-오브젝트 이름과 동일한 파일이 있는 지조사합니다.
        for(;;){
            DIR_SRUCT = readdir(DIR_PTR);
            if(DIR_SRUCT == NULL){
                break;
            }
            if(!strcmp(FILENAME,DIR_SRUCT->d_name)){
                printf("캐시 HIT 발생! 해당 요청은 캐시에서 전송합니다. %s에 대해.\n" , FILENAME);
                HIT__CACHE__TRANSMISSION(FILENAME,WEB_BROWSER_SOCKET); // 동일한 파일이 존재하면 브라우저로 파일의 내용을 전송하는 함수를 실행합니다.
                HIT_STATE = 1; // 캐쉬 힛을 1로 설정하여 오리진 서버와 연결하여 데이터를 내려받는 작업을 하지 않도록 합니다.
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
// 호스트 이름을 대입하여 아이피 주소를 얻어옵니다.
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

    int PROXY_CLIENT_SOCKET = socket(PF_INET, SOCK_STREAM, 0); // 프록시 서버와 오리진 서버의 관계에서 클라이언트로 이용할 소켓을 생성합니다.
    if (PROXY_CLIENT_SOCKET == -1) {
        perror("프록시 클라이언트 소켓 생성에 실패하였습니다. :");
        exit(1);
    }

    struct sockaddr_in ORIGIN_SERVER_ADDR;

    memset(&ORIGIN_SERVER_ADDR , 0 ,sizeof(ORIGIN_SERVER_ADDR)); // 오리진 서버의 주소 구조체를 설정합니다. 대부분의 웹 서버는 80번 포트를 이용하므로 80번을 포트로 설정하였습니다.
    ORIGIN_SERVER_ADDR.sin_family = AF_INET;
    ORIGIN_SERVER_ADDR.sin_port = htons(80);
    ORIGIN_SERVER_ADDR.sin_addr.s_addr = inet_addr(IP);

    if(connect(PROXY_CLIENT_SOCKET , (struct sockaddr*)&ORIGIN_SERVER_ADDR , sizeof(ORIGIN_SERVER_ADDR))){ // 프록시 클라이언트 소켓으로 오리진 서버와 연결합니다.
        printf("프록시 클라이언트와 오리진 서버와의 연결에 실패하였습니다. \n");
        exit(1);
    }

    if(write(PROXY_CLIENT_SOCKET , PASED , DOUBLE_BUFF_SIZE) == -1){ // 프록시 클라이언트 소켓에 파싱된 HTTP 리퀘스트를 전송합니다.
        perror("오리진 서버쪽으로 리퀘스트 전달에 실패하였습니다. ");
        exit(1);
    }

    char *RECEIVE_BUFFER = (char *)malloc(100000); // 서버 응답을 저장하기 위한 버퍼를 생성합니다.
    memset(RECEIVE_BUFFER , 0 , 100000);
    char *DATA_BUFFER = (char *)malloc(512000); // 서버의 전체 응답을 저장하기 위한 버퍼를 생성합니다.
    memset(DATA_BUFFER , 0 , 512000);

    int READ_SIZE = 0;
    int WRITE_SIZE = 0;
    int TOTAL_READ_SIZE = 0;

    int DATA_STREAM_INDEX = 0; // HTTP 응답 헤더를 추려내기 위한 변수입니다.
    char *HEADER_LINE_PTR = (char *)malloc(2000); // HTTP 응답 헤더를 저장하기 위한 버퍼입니다.
    memset(HEADER_LINE_PTR , 0 , 2000);

    char *FILENAME = (char *)malloc(QUARTER_BUFF_SIZE); // 캐시 디렉토리에 서버의 전송내용에 대한 구분을 위해서 파일 이름을 설정합니다. 그 파일 이름을 담기위한 버퍼입니다.
    memset(FILENAME , 0 , QUARTER_BUFF_SIZE);
    strcat(FILENAME,HOST);
    strcat(FILENAME,OBJECT);

    for (int i = 0; i < strlen(FILENAME); ++i) { // 파일 이름은 '/'을 허용하지 않기 때문에 '-'로 설정합니다.
        if(FILENAME[i] == '/'){
            FILENAME[i] = '-';
        }
    }

    int CACHE_STATE = 0;
    if(chdir("PROXY_CACHE") == -1){ // 캐시 디렉토리로 이동합니다.
        perror("디렉토리 이동에 실패 :");
        exit(1);
    }

    FILE *FP = fopen(FILENAME,"w+b");

    if(FP == NULL){
        perror("이진파일 생성에 오류 발생");
        exit(1);
    }

    while ((READ_SIZE = read( PROXY_CLIENT_SOCKET , RECEIVE_BUFFER , 100000)) >= 0){ // 서버로부터의 응답내용을 버퍼에 담습니다.

        if(READ_SIZE == 0){
            break;
        }
        fseek(FP , 0 , SEEK_END);

        TOTAL_READ_SIZE = TOTAL_READ_SIZE + READ_SIZE; // 서버로부터의 응답을 읽은 길이를 지속적으로 체크합니다.

        if(DATA_STREAM_INDEX == 0){ // 응답의 처음 부분에는 HTTP 리스폰스 헤더가 존재하므로 이 부분에서 응답 헤더를 가져옵니다.
            strncpy( HEADER_LINE_PTR , RECEIVE_BUFFER , 1024);
        }

       fwrite(RECEIVE_BUFFER , 1 , READ_SIZE , FP); // 생성한 파일에 응답 내용을 기록합니다.
       write(WEB_BROWSER_SOCKET , RECEIVE_BUFFER , READ_SIZE); // 동시에 웹 브라우저로 내용을 전송합니다.
       printf("응답 내용은 다음과 같습니다 \n%s\n",RECEIVE_BUFFER);
       memset(RECEIVE_BUFFER , 0 , 100000); // 전송 버퍼를 초기화 합니다.
       DATA_STREAM_INDEX++;

    }
    CACHE_VALIDATOR(TOTAL_READ_SIZE);
    if(TOTAL_READ_SIZE > MAX_OBJECT_SIZE){
        fclose(FP);
        unlink(FILENAME);
    }

    fclose(FP);
    chdir("..");
    /*
     * LRU 알고리즘 적용.
     * 1. 캐시 디렉토리를 먼저 조사한다.
     * 2. 디렉토리내 파일 용량이 5MB를 초과하지 않는지 검사한다.
     * 3. 파일에 버퍼 내용을 기록하기 전에 현재 디렉토리 사이즈 + 버퍼 내용의 길이가 5MB를 초과한다면
     * 가작 Access 타임이 오래된 파일부터 삭제 -> 용량 비교 -> 삭제를 거듭한다.
     * 4. 용량이 충분히 확보되면 파일에 내용 기록.
     */

    PROXY_LOG_WRITER(HEADER_LINE_PTR , IP , HOST , OBJECT , TOTAL_READ_SIZE); // 로그작성을 위한 함수를 실행합니다.

    shutdown(PROXY_CLIENT_SOCKET,SHUT_RDWR);
    close(PROXY_CLIENT_SOCKET); // 소켓을 닫습니다.
    free(PASED);

}
int CACHE_VALIDATOR(int TRS) {

    // 캐시에 넣을 수 있는지 조건을 확인하는 함수입니다.
    struct dirent *DIR_STRUCT;
    struct stat *FILE_STAT;

    int CACHE_STATE = 0;
    int CURRENT_CACHE_SIZE = 0;
    int FILE_SIZE = 0;

    DIR *DIR_PTR;

    char *MIN_ACC_FILE = (char *) malloc(QUARTER_BUFF_SIZE); // 최소로 접근된 파일이름을 담기위한 버퍼입니다.
    memset(MIN_ACC_FILE, 0, QUARTER_BUFF_SIZE);

    char *CWD_BUFFER = (char *) malloc(DEFAULT_BUFF_SIZE); // 작업 디렉토리 이름을 담기위한 버퍼입니다.
    memset(CWD_BUFFER, 0, DEFAULT_BUFF_SIZE);

    getcwd(CWD_BUFFER, DEFAULT_BUFF_SIZE);

    if((DIR_PTR = opendir(CWD_BUFFER)) == NULL){ // 캐시 디렉토리를 엽니다.
        perror("캐시 디렉토리 여는데에 실패 :");
        exit(1);
    }
    while ((DIR_STRUCT = readdir(DIR_PTR)) != NULL) { // 디렉토리내 파일을 조사하여 현재 캐시 사용 용량을 확인합니다.

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

    if ((CURRENT_CACHE_SIZE + TRS) <= 5000000) { // 현재 캐시 사용량 + 담을 데이터의 용량이 5MB를 초과하지 않는다면
        CACHE_STATE = 1; // 캐쉬 상태를 1로 설정한후 리턴하여 데이터를 기록하도록 합니다.
    }
    if (CURRENT_CACHE_SIZE + TRS > 5000000) { // 현재 캐시 사용량 + 담음 데이터의 용량이 5MB를 초과한다면

        if ((DIR_PTR = opendir(CWD_BUFFER)) == NULL) { // 캐시 디렉토리를 엽니다.
            perror("캐시 디렉토리를 여는데에 실패하였습니다. :");
            exit(1);
        }
        while (CURRENT_CACHE_SIZE + TRS >= 5000000) {
        // 캐쉬사이즈가 확보될 때까지 루프를 반복한다. ( 파일들을 조사하여 최소 접근 시간을 확인한다 -> 최소 접근 시간을 가진 파일을 삭제한다 -> 그래도 용량이 확보되지 않으면 이 절차를 재실행한다)
            int Inode_Pointer = 0;
            time_t MIN_ACC_TIME;
            while ((DIR_STRUCT = readdir(DIR_PTR)) != NULL) {

                if ((strcmp(DIR_STRUCT->d_name, ".") && strcmp(DIR_STRUCT->d_name, "..")) == 1) {

                    if (lstat(DIR_STRUCT->d_name, FILE_STAT) == -1) {
                        perror("파일 정보 획득에 실패 :");
                        exit(1);
                    }
                    if (Inode_Pointer == 0) { // 디렉토리 내의 첫 파일을 최소 접근 시간으로 가정합니다.
                        MIN_ACC_TIME = FILE_STAT->st_atime;
                        Inode_Pointer++;
                    }
                    if (Inode_Pointer > 0) {
                        if (MIN_ACC_TIME > FILE_STAT->st_atime) { // 다른 파일들을 조사하였을때 더 최소의 접근시간이라면 해당 접근시간을 최소 접근시간으로 설정합니다.
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
            CACHE_STATE = 1; // 용량이 확보되면 루프가 종료되면 캐시 상태를 1로 변경하여 전달한다.
    }
    closedir(DIR_PTR);
    return CACHE_STATE;
}


void PROXY_LOG_WRITER(char *HEADER_LINE , char *IP , char *HOST , char *OBJECT , int TRS){
    // 1.parse headerline
    // 2.Date : ??? EST : IP http:// HOST | OBJECT | TRS
    // 3.close FD
    int FILE_PROXYLOG_FD;
    if((FILE_PROXYLOG_FD = open( "PROXYLOG" , O_RDWR | O_CREAT | O_SYNC | O_APPEND , 0777))  == -1){ // PROXYLOG 라는 이름의 파일을 엽니다.
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
        sprintf( LOG_LINE_PTR , "NO DATE EST : %s http://%s%s => %d\n" , IP , HOST , OBJECT , TRS); // 헤더에 데이트 표기가 없다면 NO DATE로 표기
    }
    else{
        sprintf( LOG_LINE_PTR , "%s EST : %s http://%s%s => %d\n" , DATE_LINE , IP , HOST , OBJECT , TRS); // 로그 라인 버퍼에 해당 내용을 저장
    }

    if(write( FILE_PROXYLOG_FD , LOG_LINE_PTR , strlen(LOG_LINE_PTR)) == -1){ // 파일에 로그라인 버퍼내용을 기록
        perror("로그용 파일에 작성중 오류 발생. ");
        exit(1);
    }

    printf("\n\n로그를 작성중입니다 ... %s 완료!\n\n",LOG_LINE_PTR);
    close(FILE_PROXYLOG_FD); // 파일을 닫습니다.
    free(LOG_LINE_PTR);
    free(HEADER_LINE_COPY_PTR);

}

