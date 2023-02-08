#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <errno.h>
#include <unistd.h>
#include <termios.h>

typedef struct _line_list_s line_list_t;

/*
 * Lines are stored as a linked list. 
 * From a editor prespective lines should be mapped to list so 
 * if the editor is on line 3 then this should be the 2nd entry in the list 
 * assuming that entries are zero based
 */
struct _line_list_s {
	char *line;
	size_t size; //How many bytes buffer is 
	size_t useage; //How many bytes are used

	void *next;
	void *prev;
};

static struct termios ori_termios = {0};
static struct termios new_termios = {0};

enum SVIM_KEYCODE {
	KEY_NULL = 0,
	CTRL_Q = 0x11,
	CTRL_S = 0x13
};



line_list_t *svim_create_line(uint64_t size, const char *text) {
	//Check parmeter size should be more than 0 
	//and if text is not NULL then it's length
	//should be less than size 
	if(size > 0 && text && strlen(text) >= size) {
		errno = EINVAL;
		return NULL;
	}
	
	line_list_t *output = calloc(1, sizeof(line_list_t));
	if(!output) {
		return NULL;
	}
	
	output->line = calloc(sizeof(char *), size);
	if(output->line == NULL) {
		free(output->line);
		return NULL;
	}
	output->size = size;

	if(text) {
		strncat(output->line, text, size);
		output->useage = strlen(text) + 1;
	}

	return output;
}

int svim_get_termios_pair(struct termios *canonical, struct termios *raw) {
	
}

int main(int argc, char **argv) {
	line_list_t *list = svim_create_line(strlen("Hello"), "Hello");
	if(list == NULL) {
		printf("Failed to allocate line: %m\n");
		return -1;
	}
	char buf[1];
	tcgetattr(STDIN_FILENO, &ori_termios);
	new_termios = ori_termios;

	/*Configure Raw Mode*/ 
	new_termios.c_oflag &= ~(OPOST);//We want to handle this ourselves 
	new_termios.c_lflag &= ~(ICANON | ISIG | ECHO | IEXTEN); 
	new_termios.c_iflag &= ~(ISTRIP | INPCK | BRKINT | IXON);
	
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios); //Set the new termios
	while(read(STDIN_FILENO, buf, 1) != -1) {
		fprintf(stderr,"%c", buf[0]);
		if(buf[0] == CTRL_Q) {
			break;
		} else if (buf[0] == CTRL_S) {
			FILE *fp = fopen("./output", "a+");
			fseek(fp, 0, SEEK_SET);
			fwrite(list->line, sizeof(char), list->useage, fp);
			fclose(fp);
		} else {
			list->line[list->useage] = buf[0];
			list->useage++;			
		}
	}

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &ori_termios);
	return 0;
}
