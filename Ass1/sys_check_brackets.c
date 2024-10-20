#include <linux/kernel.h> // in order to use KERN_INFO, asmlinkage.. etc
#include <linux/slab.h> // in order to use kmalloc(), kfree()

asmlinkage int sys_check_brackets(const char* const str_or_null)
{
	const char* p = (const char*)str_or_null;
	char* p_cur = NULL; 
	char* p_temp = NULL;
	int RET = 1, _top;
	size_t st_size = 0, cur_size = 0;
	char* p_stack = NULL;
	if (str_or_null == NULL) {
		RET = 0; 
		return RET;
	}
    printk(KERN_INFO "Welcome to 201920791 kernel Stack\n");
	
	while (*p++ != '\0') {
		p_cur = (char*)(p - 1);
		++cur_size;
		_top = (st_size == 0) ? -1 : (st_size - 1);

    	printk(KERN_INFO "Stack: char: %c top : %d \n", *p_cur, _top);
    	printk(KERN_INFO "		cur_size: %d st_size : %d\n", cur_size, st_size);

		// RESIZE STACK
		if (cur_size > st_size) {
			// RESIZE and COPY only if it's not nullptr

			p_temp = (char*)kmalloc( ( (st_size) + 50 * sizeof(char) ), 0);
			if (p_temp == NULL) {
				return RET = -1;
			} else {
				memcpy(p_temp, p_stack, st_size * sizeof(char) );
				kfree(p_stack);
				p_stack = p_temp;
				p_temp = NULL;
			}
		}
		
		// CHECK whether PUSH OR NOT to the stack
		if (*p_cur == '(') {
			p_stack[++_top] = '(';
			++st_size;
		} else {
			// check if it's empty
			if ( st_size == 0 ) {
				RET = 0;
				return RET;
			} else {
				// POP
				p_stack[st_size--] = ' ';
			}
		}
	}
	kfree(p_stack);
	if ( st_size != 0 ) {
		RET = 0;
	}
    printk(KERN_INFO "Finished Stack! Exit code\n");
	return RET;
	
}