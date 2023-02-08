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

enum SVIM_KEYCODE {
	KEY_NULL = 0,
	CTRL_Q = 0x11,
	CTRL_S = 0x13
};

enum SVIM_MODES {
	COMMAND,
	INSERT,
	VISUAL,
	VISUAL_BLOCK,
	REPLACE,
	CTRL,
};

typedef struct _svim_ctx_s {
	enum SVIM_MODES mode;

	line_list_t *head;
	line_list_t *tail;

	line_list_t *curline;

	struct termios raw;
	struct termios canon;
	int israw; 
} svim_ctx_t;

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
	if(tcgetattr(STDIN_FILENO, canonical) < 0) { //Get the original termios 
		return -1;
	}


	*raw = *canonical;
	raw->c_oflag &= ~(OPOST);
	raw->c_lflag &= ~(ICANON | ISIG | ECHO | IEXTEN);
	raw->c_iflag &= ~(ISTRIP | INPCK | BRKINT | IXON);

	//Setup polling reads with small timeout 
	raw->c_cc[VMIN] = 0; 
	raw->c_cc[VTIME] = 0;

	raw->c_cflag |= (CS8);
}

int svim_termios_set_mode(struct termios *termios) {
	return tcsetattr(STDIN_FILENO, TCSAFLUSH, termios);
}


int main(int argc, char **argv) {
	svim_ctx_t svim_ctx = { 0 };
	int bytes;
	char buf[1];
	svim_ctx.head = svim_create_line(256, NULL);
	svim_ctx.tail = svim_ctx.head;
	svim_ctx.curline = svim_ctx.head;
	if(svim_ctx.head == NULL) {
		printf("Failed to allocate line: %m\n");
		return -1;
	}
	svim_get_termios_pair(&svim_ctx.canon, &svim_ctx.raw);
	
	svim_termios_set_mode(&svim_ctx.raw); //Set the new termios
	while((bytes = read(STDIN_FILENO, buf, 1)) != -1) {
		if(bytes == 0) {
			continue;
		} //Nothing input skip to next read;
		fprintf(stderr,"%c", buf[0]);
		if(buf[0] == CTRL_Q) {
			break;
		} else if (buf[0] == CTRL_S) {
			FILE *fp = fopen("./output", "a+");
			fseek(fp, 0, SEEK_SET);
			fwrite(svim_ctx.curline->line, sizeof(char), svim_ctx.curline->useage, fp);
			fclose(fp);
		} else {
			svim_ctx.curline->line[svim_ctx.curline->useage] = buf[0];
			svim_ctx.curline->useage++;			
		}
	}
	svim_termios_set_mode(&svim_ctx.canon);
	return 0;
}
