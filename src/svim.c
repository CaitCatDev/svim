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
typedef struct _line_s {
	char *buffer;

	size_t size; 
	size_t useage;
} line_t;

typedef struct llist_node_s {
	void *data;
	void *next;
	void *prev;
} llist_node_t;


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

	llist_node_t *head;
	llist_node_t *tail;

	llist_node_t *curline;
	
	struct termios raw;
	struct termios canon;
	int israw; 
	int run;

	int x;
	int y;
} svim_ctx_t;

line_t *svim_create_line(uint64_t size, const char *text) {
	//Check parmeter size should be more than 0 
	//and if text is not NULL then it's length
	//should be less than size
	line_t *output = NULL;
	 
	if(size > 0 && text && strlen(text) >= size) {
		errno = EINVAL;
		return NULL;
	}
	
	output = calloc(1, sizeof(line_t));
	if(!output) {
		return NULL;
	}
	
	output->buffer = calloc(sizeof(char *), size);
	if(output->buffer == NULL) {
		free(output);
		return NULL;
	}
	output->size = size;

  	//If some starting text has been passed prefill the buffer with it
	if(text) {
		strncat(output->buffer, text, size);
		output->useage = strlen(text) + 1;
	}

	return output;
}

void svim_destroy_line(line_t *line) {
	free(line->buffer);
	free(line);
}

void svim_llist_line_destroy(llist_node_t *node) {
	svim_destroy_line(node->data);
	free(node);
}

llist_node_t *svim_llist_create_line(size_t size, char *text) {
	line_t *new_line = NULL;
	llist_node_t *new_node = NULL;

	new_line = svim_create_line(size, text);
	if(!new_line) {
		return NULL;
	}
	
	new_node = calloc(1, sizeof(*new_node));
	if(!new_node) {
		svim_destroy_line(new_line);
		return NULL;
	}

	new_node->data = new_line;
	new_node->prev = NULL;
	new_node->next = NULL;

	return new_node; 
}

void svim_llist_insert_before(llist_node_t *new_node, llist_node_t *list_node) {

	//Set the previous node to point to new node 
	llist_node_t *list_prev = list_node->prev; //Get the the previous node 
	list_prev->next = new_node; 

	
	new_node->next = list_node; //New node now points to old node of this pos 
	new_node->prev = list_node->prev; //Take the old prev

	list_node->prev = new_node; //list node now points to new_node
}

void svim_llist_insert_after(llist_node_t *new_node, llist_node_t *list_node) {
	llist_node_t *next_node = list_node->next; //get the next node 
	next_node->prev = new_node; //set next node to point back to me 

	new_node->next = next_node;
	new_node->prev = list_node;

	list_node->next = new_node;
}

void svim_llist_insert_at_head(llist_node_t **head, llist_node_t *new) {
	(*head)->prev = new;
	new->next = (*head);
	new->prev = NULL;

	//Modify the head pointer refernce passed in to change 
	//head variable in the caller function 
	*head = new;
}

void svim_llist_insert_at_tail(llist_node_t **tail, llist_node_t *new) {
	(*tail)->next = new;
	new->next = NULL;
	new->prev = (*tail);

	*tail = new;
}

int svim_llist_insert_after_pos(uint64_t pos, llist_node_t *new_node, llist_node_t *head) {
	llist_node_t *tmp = head;
	for(uint64_t i = 0; i <= pos; i++) {
		if(i == pos) {
			svim_llist_insert_before(new_node, tmp);
			return 0;
		}
		tmp = tmp->next;
	}
	//Should only happen if we go outside the bounds of list
	return 1;
}

int svim_llist_insert_before_pos(uint64_t pos, llist_node_t *new_node, llist_node_t *head) {
	llist_node_t *tmp = head;
	for(uint64_t i = 0; i <= pos; i++) {
		if(i == pos) {
			svim_llist_insert_before(new_node, tmp);
			return 0;
		}
		tmp = tmp->next;
	}
	//Should only happen if we go outside the bounds of list
	return 1;
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
	return 0;
}

int svim_termios_set_mode(struct termios *termios) {
	return tcsetattr(STDIN_FILENO, TCSAFLUSH, termios);
}

int svim_llist_fwrite_line(line_t *line, FILE *fp) {
	return fwrite(line->buffer, sizeof(*line->buffer), line->useage, fp);
}

int svim_save_file(llist_node_t *head) {
	llist_node_t *tmp;
	FILE *fp = fopen("./output", "a+");
	if(!fp) { 
		printf("Failed to open output file\n");
		return -1;
	}
	fseek(fp, 0, SEEK_SET);
	
	for(tmp = head; tmp; tmp = tmp->next) {
		svim_llist_fwrite_line(tmp->data, fp);
	}

	fclose(fp);
	return 0;
}

void svim_line_set_char(char c, line_t *line) {
	line->buffer[line->useage] = c;
	line->useage++;
}

int svim_read_key() {
	char sequence[3]; //Allocate enough bytes for escape sequences
	//Escape sequences are multi byte so start by reading a character 
	if(read(STDIN_FILENO, sequence, 1) < 1) {
		return 0;
	}
	
	switch(sequence[0]) {
		case '\033': //Esacape sequence first byte
			printf("Esc");
			return '\0';
		default: //Regular character 
			return sequence[0];
	}
}

void svim_llist_line_erase_char(llist_node_t **node, int *x, int *y) {
	llist_node_t *prev = (*node)->prev;
	line_t *line = (*node)->data;

	if((*x)-1) {
		line->buffer[line->useage - 1] = '\0';
		line->useage--;
		(*x)--;
	} else if(prev) {
		line_t *tmp = prev->data;
		tmp->buffer[tmp->useage - 1] = '\0';
		tmp->useage--;
		*x = tmp->useage + 1;
		if(line->useage) {
			strncat(tmp->buffer, line->buffer, line->useage);
			tmp->useage += line->useage;
		}
		prev->next = (*node)->next;
		svim_llist_line_destroy(*node);
		*node = prev;
		(*y)--;
		
	}
}

void svim_process_key(svim_ctx_t *ctx, int key) {
	if(key == CTRL_Q) {
		ctx->run = 0;
	} else if(key == '\b' || key == 127	) {
		svim_llist_line_erase_char(&ctx->curline, &ctx->x, &ctx->y);
		ctx->tail = ctx->curline;
	} else if(key == '\n') {
		svim_line_set_char(key, ctx->curline->data);
		llist_node_t *node = svim_llist_create_line(256, NULL);
		svim_llist_insert_at_tail(&ctx->tail, node);
		ctx->y += 1;
		ctx->x = 1;
		ctx->curline = node;
	} else if (key == CTRL_S) {
		svim_save_file(ctx->head);
	} else {
		svim_line_set_char(key, ctx->curline->data);
		ctx->x += 1;
	}	
}

void svim_write_stdout(const char *str) {
	write(STDOUT_FILENO, str, strlen(str));
}

void svim_llist_write_stdout(line_t *line) {
	for(size_t i = 0; i < line->useage; i++) {
		if(line->buffer[i] == '\n' && line->useage > 1 && line->buffer[i-1] != '\r') {
			write(STDOUT_FILENO, "\r", 1);
		}
		write(STDOUT_FILENO, &line->buffer[i], 1);
	}
}

void draw_screen(svim_ctx_t ctx) {
	llist_node_t *tmp = NULL;
	svim_write_stdout("\033[2J");
	svim_write_stdout("\033[;H");
	
	for(tmp = ctx.head; tmp; tmp = tmp->next) {
		svim_llist_write_stdout(tmp->data);
	}
	printf("\033[%d;%dH", ctx.y, ctx.x);
	fflush(stdout);
}

int svim_main_loop(svim_ctx_t ctx) {
	while(ctx.run) {
		int key = svim_read_key();
		if(key == 0) {

			continue;
		}

		svim_process_key(&ctx, key);
		draw_screen(ctx);
	}
	svim_write_stdout("\033[2J");
	svim_write_stdout("\033[;H");
	return 0;
}

void svim_llist_line_destroy_list(llist_node_t *head) {
	llist_node_t *tmp = head;
	while(tmp) {
		llist_node_t *next = tmp->next;
		svim_llist_line_destroy(tmp);
		tmp = next;
	}
}


int main(int argc, char **argv) {
	svim_ctx_t svim_ctx = { 0 };
	svim_ctx.head = svim_llist_create_line(256, NULL);
	svim_ctx.tail = svim_ctx.head;
	svim_ctx.curline = svim_ctx.head;
	svim_ctx.run = 1;
	svim_ctx.x = 1;
	svim_ctx.y = 1;
	if(svim_ctx.head == NULL) {
		printf("Failed to allocate line: %m\n");
		return -1;
	}
	svim_get_termios_pair(&svim_ctx.canon, &svim_ctx.raw);
	
	svim_termios_set_mode(&svim_ctx.raw); //Set the new termios

	svim_main_loop(svim_ctx);
	svim_llist_line_destroy_list(svim_ctx.head);
	svim_termios_set_mode(&svim_ctx.canon);
	return 0;
}
