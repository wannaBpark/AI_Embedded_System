#include <linux/fs.h>   /* open( ), read( ), write( ), close( ) 커널 함수 */
#include <linux/cdev.h> /* 문자 디바이스 */
#include <linux/module.h>
#include <linux/io.h>        /* ioremap( ), iounmap( ) 커널 함수 */
#include <linux/uaccess.h>   /* copy_to_user( ), copy_from_user( ) 커널 함수 */
#include <linux/gpio.h>      /* GPIO 함수 */
#include <linux/interrupt.h> /* 인터럽트 처리를 위한 헤더 파일 */

#include <linux/timer.h>     /* 타이머 기능의 사용을 위한 헤더 파일 */
#include <linux/mutex.h>
#include <linux/string.h>       /* 문자열 분석을 위한 함수를 위한 헤더 파일 */
#include <linux/sched.h>        /* send_sig_info( ) 함수를 위한 헤더 파일 */
#include <linux/sched/signal.h> /* 시그널 처리를 위한 헤더 파일 */

#include <linux/random.h>             /* get_random_bytes() - 난수 생성을 위한 헤더 파일 */

/* 디바이스 파일의 주 번호와 부 번호 */
#define GPIO_MAJOR 200
#define GPIO_MINOR 0
#define GPIO_DEVICE "gpioled"      /* 디바이스 디바이스 파일의 이름 */
#define str(s) (#s)
#define combine(a, b) (a##b)
#define GPIO_SW(n) (GPIO_SW##n)
#define GPIO_LED(n) (GPIO_LED##n)

/* 사용자에게 보낼 시그널 정의 */
#define SIGLEDEND 2
#define SIGCORRECT 8
#define SIGWRONG 28

/* 사용자 입력 저장할 arr_input[]의 최대 길이 정의 */
#define MXINPUT 10

#define GPIO_LED1 13                /* LED 사용을 위한 GPIO의 번호 */
#define GPIO_LED2 19
#define GPIO_LED3 26
#define GPIO_SW1  16                /* 스위치에 대한 GPIO의 번호 */
#define GPIO_SW2  20
#define GPIO_SW3  21
#define GPIO_SW4  18

static char msg[BLOCK_SIZE] = {0}; /* write( ) 함수에서 읽은 데이터 저장 */

static int arr_LED[3] = {GPIO_LED1, GPIO_LED2, GPIO_LED3};
static int arr_SW[4] = {GPIO_SW1, GPIO_SW2, GPIO_SW3, GPIO_SW4};
static int arr_input[MXINPUT] = {0,};

static int Sequence[10] = {0,};
static int Step = 1;               /* 현재 LED 게임이 몇 단계인지 저장할 변수 */
static int SeqLength;
static int SeqIdx = 0;
static int Idx_UserInput = 0;
static int isLighting = 0; /* 4번째 단계 : 0.5초 동안 점등 중인지 저장하는 변수*/

/* 입출력 함수를 위한 선언 */
static int gpio_open(struct inode *, struct file *);
static ssize_t gpio_read(struct file *, char *, size_t, loff_t *);
static ssize_t gpio_write(struct file *, const char *, size_t, loff_t *);
static int gpio_close(struct inode *, struct file *);

/* Assignment 2 구현용 함수들 전방 선언 */
static inline int get_sequence_length(void);
static inline void decide_random_order(void);
static inline void my_timer_setup(void);
static inline int is_input_correct(void);

/* 유닉스 입출력 함수들의 처리를 위한 구조체 */
static struct file_operations gpio_fops = {
    .owner = THIS_MODULE,
    .read = gpio_read,
    .write = gpio_write,
    .open = gpio_open,
    .release = gpio_close,
};

/* 뮤텍스의 사용을 위한 헤더 파일 */
struct cdev gpio_cdev;
static int swirq[5] = {0,};
static struct timer_list timer; /* 타이머 처리를 위한 구조체 */
static struct task_struct* task = NULL;
static pid_t pid = -1;
static DEFINE_MUTEX(led_mutex); /* 충돌 방지를 위한 커널 뮤텍스 */

static void timer_func(struct timer_list* t);
static void wait_timer_func(struct timer_list* t);

/* 2번째 단계 : 1초 점등 타이머 처리를 위한 함수 */
static void timer_func(struct timer_list* t)
{
    int cur_LED_Idx = Sequence[SeqIdx];
    int cur_LED = arr_LED[cur_LED_Idx];
    
    /* 뮤텍스를 이용한 충돌 처리 */
    if (mutex_trylock(&led_mutex) != 0) { 
        gpio_set_value(cur_LED, 1); /* 현재 LED 점등 */
        mutex_unlock(&led_mutex);
    }
    
    ++SeqIdx;
    /* 다음 실행을 위한 타이머 설정 */ 
    timer.function = wait_timer_func;
    mod_timer(&timer, jiffies + (1 * HZ));
}

/* 
2, 3번째 단계 : 0.5초 기다리는 타이머 처리를 위한 함수
점등이 끝나면 Ready for siwtch input 전달
*/
static void wait_timer_func(struct timer_list* t)
{
    int prev_LED_Idx = Sequence[SeqIdx - 1];
    int prev_LED = arr_LED[prev_LED_Idx];
    int ret;

    /* 뮤텍스를 이용한 충돌 처리 */
    if (mutex_trylock(&led_mutex) != 0) {      
        gpio_set_value(prev_LED, 0);        /* 직전 점등된 LED 꺼주기 */
        mutex_unlock(&led_mutex);
    }

    /* 아직 점등할 LED 남아있을 때, 다음 점등을 위한 타이머 설정 */ 
    if (SeqIdx < SeqLength) {
        timer.function = timer_func;
        mod_timer(&timer, jiffies + (HZ / 2));
    } else {
        SeqIdx = 0;
        Step = 3;

        // 사용자에게 LED 점등이 끝났다고 시그널로 알림
        if (task) {
            static struct kernel_siginfo sinfo; /* 시그널 처리를 위한 구조체 */
            memset(&sinfo, 0, sizeof(struct kernel_siginfo));
            sinfo.si_signo = SIGLEDEND; 
            sinfo.si_code = SI_USER; // 시그널 발생 이유
            ret = send_sig_info(SIGLEDEND, &sinfo, task); /* 해당 프로세스에 시그널 보내기 */

            printk("SEND Signal RESULT : %d", ret);
        } else {
            printk("Send Signal Failed bc of missing task\n");
        }
        
    }
}

/* 4번째 단계 : 이미 점등된 LED를 0.5초뒤 끄는 timer function() */
static void led_light_by_switch_timer_func(struct timer_list* t)
{
    static int cur_LED = -1;
    
    
    /* 뮤텍스를 이용한 충돌 처리 */
    if (mutex_trylock(&led_mutex) != 0) {   
        /* 이미 0.5초가 지났고, cur_LED에 input 해당 LED가 켜져있는 상태면 끈다 */
        if (cur_LED != -1) {
            gpio_set_value(cur_LED, 0);
            mutex_unlock(&led_mutex);
            isLighting = 0;
            cur_LED = -1;
            return;
        }

        /* interrupt handler에서 켜진 LED를 input 배열로부터 찾고, cur_LED에 정보를 저장한다 */
        if (Idx_UserInput == 0) return;

        cur_LED = arr_LED[arr_input[Idx_UserInput - 1]];
        gpio_set_value(cur_LED, 1);
        mutex_unlock(&led_mutex);
        mod_timer(&timer, jiffies + (HZ / 2));
    }
       
}

/* 입력 완료 스위치 SW4 에 대한 인터럽트 핸들러*/
static irqreturn_t isr_func(int irq, void *data)
{
    int sig_result, ret;
    /* debouncing 해결용 timeout && 4번째 단계가 아니면 인터럽트 무시함 */
    static unsigned long timeout = 0;
    if (timeout != 0 && jiffies <= timeout && Step != 4) {
        return IRQ_NONE;
    } else {
        timeout = jiffies + (HZ / 4);
    }
    
    /* 입력 일치 여부에 따라 sig_result에 값 할당 */
    sig_result = is_input_correct() == 0 ? SIGWRONG : SIGCORRECT;
    printk("sigresult : %s pid : %d\t is task null? : %d\n", 
            sig_result == SIGWRONG ? "wrong!\n" : "correct!\n", pid, task == NULL);

    if (task != NULL) {
        static struct kernel_siginfo sinfo; /* 시그널 처리를 위한 구조체 */
        memset(&sinfo, 0, sizeof(struct kernel_siginfo));
        sinfo.si_signo = sig_result; // 시그널 넘버 
        sinfo.si_code = SI_USER; // 시그널 발생 이유
        
        ret = send_sig_info(sig_result, &sinfo, task); /* 해당 프로세스에 시그널 보내기 */
        printk("SEND Signal RESULT : %d", ret);
    } else {
        printk("Couldn't send sig bc of missing task\n");
    }
    Step = 0;
    Idx_UserInput = 0;
    
    return IRQ_HANDLED;
}

/* 4번째 단계에서, 1~3번 스위치 입력에 대한 LED 점등 */
static irqreturn_t isr_sw_func(int irq, void *data)
{
    static unsigned long timeout = 0; 
    size_t curIdx;
    if ( (timeout != 0 && jiffies <= timeout) || isLighting) {
        return IRQ_NONE;
    } else {
        timeout = jiffies + (HZ / 5);
    }
    if (mutex_trylock(&led_mutex) != 0) { 
        
        /* 사용자 입력 길이가 7이상이면, 10까지만 받는다 || 현재 Step이 4가 아니면 LED를 키지 않는다 */
        if (Idx_UserInput >= MXINPUT || Step != 4){
            mutex_unlock(&led_mutex);
            return IRQ_HANDLED;
        }
        for(size_t i = 0; i < 3; ++i) {
            if (irq == swirq[i+1] && !gpio_get_value(arr_LED[i])) {
                curIdx = i;
                break;
            }
        }
        arr_input[Idx_UserInput++] = curIdx;

        timer.expires = jiffies;
        timer.function = led_light_by_switch_timer_func;
        isLighting = 1;
        mutex_unlock(&led_mutex);
        add_timer(&timer);
    }
    return IRQ_HANDLED;
}

int initModule(void)
{
    dev_t devno;
    size_t count = 1, i;
    int err;
    printk(KERN_INFO "signal module!\n");

    mutex_init(&led_mutex); /* 뮤텍스를 초기화한다. */
    devno = MKDEV(GPIO_MAJOR, GPIO_MINOR);
    register_chrdev_region(devno, 1, GPIO_DEVICE);

    /* 문자 디바이스를 위한 구조체를 초기화한다. */
    cdev_init(&gpio_cdev, &gpio_fops);
    gpio_cdev.owner = THIS_MODULE;
    err = cdev_add(&gpio_cdev, devno, count); /* 문자 디바이스를 추가한다. */
    if (err < 0) {
        printk("Error : Device Add\n");
        return -1;
    }

    /* LED GPIO 사용을 요청, 값 초기화 */
    for (i = 0; i < 3; ++i) {
        gpio_request(arr_LED[i], str(LEDi));
        gpio_direction_output(arr_LED[i], 0);
        gpio_set_value(arr_LED[i], 0);
    }

    /* 스위치 GPIO 사용 요청 */
    for (i = 0; i < 4; ++i) {
        gpio_request(arr_SW[i], str(SWITCH));
    }
    
    /* 스위치 1 ~ 4 인터럽트 핸들러 등록*/
    swirq[4] = gpio_to_irq(GPIO_SW(4)); /* GPIO 인터럽트 번호 획득 : 스위치4 */
    err = request_irq(swirq[4], isr_func, IRQF_TRIGGER_RISING, str(SWITCH4), NULL); 
    for (i = 1; i <= 3; ++i) {
        swirq[i] = gpio_to_irq(arr_SW[i-1]); /* 스위치 1~3 */
        err = request_irq(swirq[i], isr_sw_func, IRQF_TRIGGER_RISING, str(SWITCH), NULL);
        /* GPIO 인터럽트 핸들러 등록 */
        if (err < 0)
            return -1;
    }

    pid = -1;
    Step = 0;
    
    return 0;
}
void cleanupModule(void)
{
    dev_t devno = MKDEV(GPIO_MAJOR, GPIO_MINOR);

    mutex_destroy(&led_mutex); /* 뮤텍스를 해제한다. */

    del_timer_sync(&timer); /* 등록했던 타이머를 삭제한다 */

    unregister_chrdev_region(devno, 1); /* 문자 디바이스의 등록을 해제한다. */
    cdev_del(&gpio_cdev); /* 문자 디바이스의 구조체를 해제한다. */
    
    /* 사용이 끝난 인터럽트 해제 */
    for (size_t i = 1; i <= 4; ++i) {
        free_irq(swirq[i], NULL);
    }

    /* 더 이상 필요없는 LED, SWITCH 자원을 해제한다. */
    for (size_t i = 0; i < 3; ++i) {
        gpio_free(arr_LED[i]); 
        gpio_free(arr_SW[i]);  
    }
    gpio_free(GPIO_SW(4));
    /* LED 3개, 스위치 4개 해제 */

    pid = -1;
    Step = 0;
    printk(KERN_INFO "Good-bye module!\n");
}

static int gpio_open(struct inode *inod, struct file *fil)
{
    printk("GPIO Device opened(%d/%d)\n", imajor(inod), iminor(inod));
    try_module_get(THIS_MODULE);
    return 0;
}
static int gpio_close(struct inode *inod, struct file *fil)
{
    printk("GPIO Device closed(%d)\n", MAJOR(fil->f_path.dentry->d_inode->i_rdev));
    module_put(THIS_MODULE);
    return 0;
}
static ssize_t gpio_read(struct file *inode, char *buff, size_t len, loff_t *off)
{
    int count;
    strcat(msg, " from Kernel");
    count = copy_to_user(buff, msg, strlen(msg) + 1); /* 사용자 영역으로 데이터를 보낸다. */
    printk("GPIO Device(%d) read : %s(%d)\n", MAJOR(inode->f_path.dentry->d_inode->i_rdev), msg, count);
    return count;
}

static ssize_t gpio_write(struct file *inode, const char *buff, size_t len, loff_t *off)
{
    size_t count;
    char *cmd, *str;
    char *sep = ":";
    char *endptr, *pidstr;

    memset(msg, 0, BLOCK_SIZE);
    count = copy_from_user(msg, buff, len); /* 사용자 영역으로부터 데이터를 가져온다. */

    str = kstrdup(msg, GFP_KERNEL);
    cmd = strsep(&str, sep);
    pidstr = strsep(&str, sep);
    printk("Command : %s, Pid : %s\n", cmd, pidstr);
    printk("Read from User: \n msg : %s \t cmd : %s \n strcmp result : %d\n", msg, cmd,
            strcmp(cmd, str(Ready for switch input)) );
    
    printk("strcmp Game start result : %d\n cmd : %s \n compare with%s",
            strcmp(cmd, "Game start"), cmd, "Game start");

    /* 1번째 단계 : Game Start:pid 를 입력 받음*/
    if (!strcmp(cmd, str(Game start)) ) {
        printk("Kernel : Game has been started\n");
        
        /* pid 프로세스의 task 처리 : pidstr 검사 && Game start 일 때만 pid 대입.
            pid : 맨 처음 Game Start:pid 형태로 같이 들어옴*/
        if (pidstr != NULL) { 
            printk("GPIO Device(%d) write : %s(%d)\n",
                MAJOR(inode->f_path.dentry->d_inode->i_rdev), 
                msg, 
                len);
            pid = simple_strtol(pidstr, &endptr, 10);
            printk("Game start, PID is :%d\n", pid);
            if (endptr != NULL) {
                task = pid_task(find_vpid(pid), PIDTYPE_PID);
                if (task == NULL) {
                    printk("Error : Can't find PID from user application\n");
                    return 0;
                }
            }
        }
        Step = 2;
    }
    
    
    /* 2번째 단계 : 랜덤 시퀀스 길이 결정, 시퀀스 해당 LED 점등 */
    if (Step == 2) {
        SeqLength = get_sequence_length();  /* 시퀀스 길이 결정 3 ~ 7*/
        decide_random_order();              /* 시퀀스길이 만큼 랜덤 시퀀스 저장*/
        my_timer_setup();                   /* 점등을 위한 timer 세팅 후, 점등 시작*/
    }

    /* 3번째 단계 : Ready for switch input이 들어오면, 4단계로 넘어감
       해당 str 이전의 모든 입력은 무시됨 */    
    if (Step == 3 && !strcmp(cmd, str(Ready for switch input))) {
        Step = 4;
        Idx_UserInput = 0;
        printk("Step changed : %d and input idx : %d", Step, Idx_UserInput);
    }
    return count;
}

/* 시퀀스 길이를 결정하는 함수 */
static inline int get_sequence_length(void)
{
    size_t num;
    get_random_bytes(&num, sizeof(size_t));
    return num % 5 + 3;
}

/* 랜덤 시퀀스를 결정하는 함수 */
static inline void decide_random_order(void)
{
    size_t num;
    printk("Sequence Length : %d", SeqLength);
    for (size_t i = 0; i < SeqLength; ++i) {
        get_random_bytes(&num, sizeof(size_t));
        Sequence[i] = num % 3;
        printk("Seq [%d]: %d\n", i, Sequence[i] + 1);
    }
    return;
}

/* 2단계의 LED 시퀀스 점등을 위한 타이머 세팅 */
static inline void my_timer_setup(void)
{
    SeqIdx = 0;
    timer_setup(&timer, timer_func, 0);
    /* timer_list 구조체 초기화 : 게임
        timer 만료 후, timer_func 콜백 호출*/
    timer.expires = jiffies + (1 * HZ);
    add_timer(&timer); /* 타이머 추가 */
}

/* 사용자 입력과 실제 시퀀스를 비교하여 1 또는 0 반환 */
static inline int is_input_correct(void)
{
    int ret = 1;
    printk("User Input Length : %d \t Actual Sequence Length : %d\n", Idx_UserInput, SeqLength);
    /* 사용자의 입력 길이 != 시퀀스 길이 이면, 오답이다 */
    if (Idx_UserInput != SeqLength) {
        ret = 0;
    }

    for (size_t i = 0; i < SeqLength; ++i) {
        // 입력과 시퀀스가 불일치 하면, false 반환
        printk("i th idx input value : input %d\t sequence %d\n", arr_input[i], Sequence[i]);
        if (arr_input[i] != Sequence[i]) { 
            ret = 0;
        }
    }
    return ret;
}

module_init(initModule);
module_exit(cleanupModule);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Heonwoo Lee");
MODULE_DESCRIPTION("Raspberry Pi GPIO LED Game Device Module for Assignment #2");