#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#define STR_APP_START "Please focus on the LED lights…\n"
#define STR_GAME_START "Game start"
#define STR_PRESS_SWITCH "Please press the switches in the order you remember. \n"
#define STR_READY_SWITCH "Ready for switch input"
#define STR_CORRECT "Your input is correct.\n"
#define STR_WRONG "Your input is wrong\n"

/* 디바이스 드라이버로부터 받을 시그널 정의 */
#define SIGLEDEND 2
#define SIGCORRECT 8
#define SIGWRONG 28

static char buf[BUFSIZ];
static int fd = -1;

/* LED 점등이 끝난 후 3단계 시그널 처리를 위한 핸들러 */
void led_complete_handler(int signum)
{
    if (signum == SIGLEDEND) {
        printf("%s", STR_PRESS_SWITCH);

        /* 디바이스 드라이버에게 Ready for switch input 문자열 전달 */
        memset(buf, 0, BUFSIZ);
        sprintf(buf, "%s", STR_READY_SWITCH);
        write(fd, buf, strlen(buf));
    }
    else {
        exit(1);
    }
}

/* 5단계 : Switch D 누르면 올 시그널을 처리할 함수 */
void input_result_handler(int signum)
{
    // printf("input result SIG received! :%d\n", signum);
    if (signum == SIGCORRECT) {
        printf(STR_CORRECT); // 맞았다는 시그널 -> correct! 출력
    } else if (signum == SIGWRONG) {
        printf(STR_WRONG); // 틀렸다는 시그널 -> wrong! 출력
    }
    close(fd);
    exit(1);
}

int main(int argc, char **argv)
{
    
    char i = 0;
    
    memset(buf, 0, BUFSIZ);
    signal(SIGLEDEND, led_complete_handler); /* LED 점등 완료 시그널 처리 위한 핸들러 등록 */
    signal(SIGCORRECT, input_result_handler); /* 디바이스 드라이버로부터 입력결과를 받을 핸들러 등록 */
    signal(SIGWRONG, input_result_handler); 

    printf(STR_APP_START); // Please Focus on the Lights.. 출력
    fd = open("/dev/gpioled", O_RDWR);
    sprintf(buf, "%s:%d", STR_GAME_START, getpid());
    write(fd, buf, strlen(buf)); // Step 1 : 문자열 출력 후, 디바이스 드라이버에 "Game start" 전달
    
    while (1); /* pause(); */
    close(fd);
    
    return 0;
}