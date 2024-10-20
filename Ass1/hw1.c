#include <unistd.h>
#include <linux/unistd.h>
#include <stdio.h>

int check_brackets(const char* const str)
{
    return syscall(__NR_check_brackets, str);
}
int main()
{
	char A[5000000];
	// printf("INPUT : ");
	scanf("%s", A);

	int ret = check_brackets(A);

	printf("%d\n", ret);
    
    return 0;
}

