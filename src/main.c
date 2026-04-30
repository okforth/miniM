#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "ANSI_Escape_Sequences.h"

#include <string.h>

#define DEBUG_MODE 0

#define MAX_PATH_LEN 256 //make it more safe later
#define BUFFER_LEN 16384
#define STARTING_TEXT_LINES 8
#define STARTING_LINE_LEN 32

// Terminal key comb. (signals) and their corresponding ASCII unprintable control codes
#define CTRL_S 19
#define CTRL_O 15
#define CTRL_Q 17
#define CTRL_N 14
#define CTRL_T 20
#define KEY_ENTER 13  // CR
#define KEY_BACKSPACE 127  // DEL

static struct termios original_termios;
int tty_fd;

struct editor_state{
    char **text_lines;
    int *allocated_char_counts;
    int *actual_char_counts;
    int line_count;
    int line_number;
    int char_number;
    int upper_screen_bound;
    char c;
    char prev_c;
    char is_CSI;
    char curr_path[MAX_PATH_LEN];
};

int key_handling(struct editor_state *e, int lite_mode_flag);

int open_new_file_logic(struct editor_state *e);
int open_file_logic(struct editor_state *e);
int save_file_logic(struct editor_state *e);
int alloc_mem_for_text(struct editor_state *e);
int free_text_mem(struct editor_state *e);

int file_opened_flag = 0;
int file_saved_flag = 0;
int file_open_error_flag = 0;
int overtype_mode = 0;
int input_mode_change_flag = 0; //typing mode (insert/overtype) change information flag
int has_line_changed = 0; // Checks in case currently pointed at text line changed.
int return_to_editor_screen = 0; // Checks if user returned to editor from different prompt/screen (because it needs additional refresh then).

void disable_raw_mode() {
    if (tcsetattr(tty_fd, TCSAFLUSH, &original_termios) == -1) {
        perror("tcsetattr restore");
    }
}

void enable_raw_mode() {
        //We first remember it with tcgetattr() by writing to orginal_termios termios struct,
        //in order to restore the terminal back to its user's previous state (with use of disable_raw_mode() function).
    tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd == -1) {
        perror("open /dev/tty");
        exit(1);
    }
    
    if (tcgetattr(tty_fd, &original_termios) == -1) {
        perror("tcgetattr");
        exit(1);
    }

    atexit(disable_raw_mode);

    struct termios raw = original_termios;

    // Input flags (disabling)
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // Output flags (disabling)
    raw.c_oflag &= ~(OPOST);

    // Control flags (enabling)
    raw.c_cflag |= (CS8);

    // Local flags (disabling)
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // Control chars
    raw.c_cc[VMIN] = 1;   // read blocks until 1 byte
    raw.c_cc[VTIME] = 0;  // no timeout

    if (tcsetattr(tty_fd, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(1);
    }
}

volatile sig_atomic_t window_resized = 0;

void handle_sigwinch(int signo) { // i.e: handle a SIGnal of WINdow CHange
    (void)signo;
    window_resized = 1;
}

int get_window_size(struct winsize *ws) {


    if (ioctl(tty_fd, TIOCGWINSZ, ws) == -1 || ws->ws_col == 0) {
        return -1; //error
    }

    return 0;
}

int get_cursor_position(int fd, int *row, int *col) {

    char buf[32];
    unsigned int i = 0;

    // Asking terminal: "where is cursor?"
    if (write(fd, "\e[6n", 4) != 4) {
        return -1;
    }

    // Read response: \e [ row ; col R
    while (i < sizeof(buf) - 1) {
        if (read(fd, &buf[i], 1) != 1) {
            break;
        }
        if (buf[i] == 'R') {
            break;
        }
        i++;
    }

    buf[i + 1] = '\0';

    // Expected format: \e [ rows ; cols R
    if (buf[0] != '\e' || buf[1] != '[') {
        return -1;
    }

    if (sscanf(&buf[2], "%d;%d", row, col) != 2) {
        return -1;
    }

    return 0;
}

//int input_file_name_logic(char curr_path[]){
int input_file_name_logic(struct editor_state *e){

    struct editor_state e_temp;
    strcpy(e_temp.curr_path, e->curr_path);
    e_temp.prev_c = '\0';
    e_temp.is_CSI = 0;
    e_temp.char_number = 0;
    e_temp.line_number = 0;
    e_temp.line_count = 1;
    e_temp.allocated_char_counts = calloc(e_temp.line_count, sizeof(int));
    e_temp.allocated_char_counts[0] = STARTING_LINE_LEN;
    e_temp.actual_char_counts = calloc(e_temp.line_count, sizeof(int));
    e_temp.actual_char_counts[0] = 0;
    e_temp.text_lines = calloc(e_temp.line_count, sizeof(char*)); //has only 1 line (and is handled as such), but made as char** for compatibility with key_handling() function.
    e_temp.text_lines[0] = calloc(STARTING_LINE_LEN, sizeof(char));
    printf(SAVE_CURSOR_POS);
    while(1){
        if (read(tty_fd, &(e_temp.c), 1) == 1) {
            key_handling(&e_temp, 1);
            e_temp.prev_c = e_temp.c;
            printf(REFRESH_ENTIRE_LINE); // Can make it more effecting by refreshing only part of the line later.
            printf(RESTORE_CURSOR_POS);
            printf("%s", e_temp.text_lines[0]); // printing currently written path name's text
            if(e_temp.c == 13 /*CR*/){ // if enter (return) is pressed stop waiting for more characters and accept the given file name
                e->curr_path[e_temp.allocated_char_counts[0] - 1] = '\0'; //getting rid of CR character at the end of file name.
                break;
            }
        }
    }

    strcpy(e->curr_path, e_temp.text_lines[0]);

    free_text_mem(&e_temp);

    return 0;
}

int open_new_file_logic(struct editor_state *e){
    
    char temp_c;
    printf(FULL_SCREEN_REFRESH);
    printf("Are you sure you wanna open new file? This will delete all unsaved progress on current file [Y/n]");
    while(1){
        if(read(tty_fd, &temp_c, 1) == 1){
            if(temp_c == 'Y' || temp_c == 'y' || temp_c == '\r'){

                e->line_count = STARTING_TEXT_LINES;
                
                free_text_mem(e);

                alloc_mem_for_text(e);
                
                e->line_number = 0;
                e->char_number = 0;
                e->line_count = STARTING_TEXT_LINES;
                //line_count being cached as zero'ed and allocated later faster would be nice (instead of freeing them and mallocing them later)
                strcpy(e->curr_path, "");
                break;
            }else if(temp_c == 'n'){
                break;
            }
        }
    }

    printf(FULL_SCREEN_REFRESH);
    return_to_editor_screen = 1;

    return 0;
}

// Reallocs memory for text_lines, allocated char_counts and actual_char_counts
int grow_line_count(struct editor_state *e){

    char **extended_text_lines = realloc(e->text_lines, e->line_count * 2 * sizeof(char*));
    int *extended_allocated_char_counts = realloc(e->allocated_char_counts, e->line_count * 2 * sizeof(int));
    int *extended_actual_char_counts = realloc(e->actual_char_counts, e->line_count * 2 * sizeof(int));
    if (extended_text_lines == NULL){ // Safety measurements
        perror("unable to perform realloc for extended_text_lines!");
        exit(1);
        //! Do some kind of emergency save later here
    }else if (extended_allocated_char_counts == NULL){
        perror("unable to perform realloc for extended_allocated_char_counts!");
        exit(1);
    }else if (extended_actual_char_counts == NULL){
        perror("unable to perform realloc for extended_actual_char_counts!");
        exit(1);
    }else{

        e->text_lines = extended_text_lines; // New char** pointer appended with new few lines (multiplying current number of lines by 2)
        e->allocated_char_counts = extended_allocated_char_counts;
        e->actual_char_counts = extended_actual_char_counts;
        for(int i = e->line_count; i < e->line_count * 2; i++){
            e->text_lines[i] = calloc(STARTING_LINE_LEN, sizeof(char));
            e->allocated_char_counts[i] = STARTING_LINE_LEN;
            e->actual_char_counts[i] = 0;
        }

        e->line_count *= 2;
    }

    return 0;
}

// Grows current line length/size (reallcos memory for it)
int grow_curr_line_len(struct editor_state *e){

    int new_size_count = e->allocated_char_counts[e->line_number] * 2;

    char *extended_line_size = realloc(e->text_lines[e->line_number], new_size_count * sizeof(char));
    if (extended_line_size == NULL) {
        perror("realloc for growing line length!");
        exit(1);
        //! Do some kind of emergency save later here
    }

    e->text_lines[e->line_number] = extended_line_size;
    for(int i = e->allocated_char_counts[e->line_number]; i < new_size_count; i++)
        e->text_lines[e->line_number][i] = '\0';
                // memcpy/memset would be more optimal tbh
    e->allocated_char_counts[e->line_number] = new_size_count;

    return 0;
}

int open_file_logic(struct editor_state *e){

    printf(FULL_SCREEN_REFRESH);
    printf("Please input the name or path and name of file you want to open (You can use relative or absolute path):\r\n\r\n");
    input_file_name_logic(e);

    int fd = open(e->curr_path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        file_open_error_flag = 1;
        return_to_editor_screen = 1;
        return -1;
    }

    free_text_mem(e);

    e->line_count = STARTING_TEXT_LINES;
    alloc_mem_for_text(e);

    char buffer[BUFFER_LEN]; //Make it safer later!!!!!
    ssize_t bytes_read;

    e->char_number = 0;
    e->line_number = 0;
    e->actual_char_counts[e->line_number] = 0;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {

        for (int i = 0; i < bytes_read; i++) {
            char c = buffer[i];

            // New line
            if (c == '\n') {
                e->text_lines[e->line_number][e->char_number] = '\0';
                e->line_number += 1;
                e->char_number = 0;
                e->actual_char_counts[e->line_number] = 0;

                if(e->line_number >= e->line_count - 1){

                    grow_line_count(e);

                }

            }else{

                // grow line if needed
                if (e->char_number >= e->allocated_char_counts[e->line_number] - 1) {

                    grow_curr_line_len(e);
                }

                // Add char to current line
                e->text_lines[e->line_number][e->char_number] = c;
                e->char_number += 1;
                e->actual_char_counts[e->line_number] += 1;
            }
        }
    }

    // null-terminate last line
    e->text_lines[e->line_number][e->char_number] = '\0';

    close(fd);
    file_opened_flag = 1;
    return_to_editor_screen = 1;

    printf(FULL_SCREEN_REFRESH);

    return 0;
}

int save_file_logic(struct editor_state *e){

    printf(FULL_SCREEN_REFRESH);

    if(strcmp(e->curr_path, "") == 0){
        printf("Please input the saved file name or path and name (You can use relative or absolute path):\r\n\r\n");
        input_file_name_logic(e);
    }

    int fd = open(e->curr_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        return_to_editor_screen = 1;
        return -1;
    }

    int max_line_length = 1;

    for(int i = 0; i < e->line_count; i++){
        if(e->allocated_char_counts[i] > max_line_length)
            max_line_length = e->allocated_char_counts[i];
    }

    char buffer[max_line_length];

    ssize_t bytes_written = 0;

    for(int i = 0; i < e->line_count; i++){

        size_t len = strlen(e->text_lines[i]);

        strcpy(buffer, e->text_lines[i]);
        buffer[len] = '\n';
        buffer[len+1] = '\0';

        bytes_written = write(fd, buffer, strlen(buffer));
        if (bytes_written == -1) {
            perror("write");
            close(fd);
            return -1;
        }
        if (bytes_written != strlen(buffer)) {
            fprintf(stderr, "Partial write!\n");
            close(fd);
            return -1;
        }

    }

    close(fd);
    file_saved_flag = 1;
    return_to_editor_screen = 1;

    printf(FULL_SCREEN_REFRESH);

    return 0;
}

int arrow_key_handling(struct editor_state *e){

    switch(e->c){ // Arrow keys

        case 'A': // Arrow Up

            if(e->line_number <= 0){
                ;
            }else{
                e->line_number -= 1;
                e->char_number = 0;
            }

            break;

        case 'B': // Arrow Down

            int are_lines_empty = 1; 
            for(int i = e->line_number + 1; i < e->line_count; i++){
                //Checking if all the proceeding lines are empty.
                //if that's the case prevent from descending down.
                if(e->text_lines[i][0] != '\0'){
                    are_lines_empty = 0;
                    break;
                }
            }
            if(are_lines_empty){
                break;
            }
            if(e->line_number + 1 >= e->line_count){
                ;
            }else{
                e->line_number += 1;
                e->char_number = 0;
            }

            break;

        case 'C': // Arrow Right

            // ALLOW FOR CURSOR TO BE AT THE END OF FILE
            if(e->char_number + 1 >= e->allocated_char_counts[e->line_number] || e->text_lines[e->line_number][e->char_number] == '\0'){
                ;
            }else{
                e->char_number += 1;
            }

            break;

        case 'D': // Arrow Left

            if(e->char_number <= 0){
                ;
            }else{
                e->char_number -= 1;
            }

            break;

    }

    return 0;

}

//                                                  v lite mode is used for key handling in options like: setting file name to save it etc. text areas 
int key_handling(struct editor_state *e, int lite_mode_flag){

    if(lite_mode_flag == 0){ // If lite mode isn't present handle all the special characters as well.

        if (e->c == CTRL_S) { // Saves to a file

            save_file_logic(e);
            return 0;

        }else if (e->c == CTRL_O) { // Opens a file

            open_file_logic(e);
            return 0;

        }else if (e->c == CTRL_Q) { // Quits program

            return -1; // Quit program

        }else if (e->c == CTRL_N) { // Opens new file

            open_new_file_logic(e);
            return 0;

        }else if (e->c == KEY_ENTER) { // <- CR "return" handling

            // Adding (allocating more) lines to text_lines line array in case it's needed.
            if(e->line_number >= e->line_count-1){// Safety measurements
                
                grow_line_count(e);

            }

            e->line_number += 1;
            has_line_changed = 1;
            e->char_number = 0;

            e->text_lines[e->line_number][e->char_number] = '\0';
            
            return 0;

        }

    }

    if (e->c == KEY_BACKSPACE) { // <- DEL ("backspace") handling

        if(e->char_number < e->actual_char_counts[e->line_number]){

		    // DELETE CHARACTER LEFT-SIDE OF CURSOR
            for(int i = e->char_number - 1; i < e->actual_char_counts[e->line_number] - 1; i++){
                e->text_lines[e->line_number][i] = e->text_lines[e->line_number][i+1];
            }

            e->actual_char_counts[e->line_number] -= 1;
            e->text_lines[e->line_number][e->actual_char_counts[e->line_number]] = '\0';

            // UPDATE CURSOR POSITION AFTER BACKSPACE
            if (e->char_number > 0) {
                e->char_number -= 1;
            }

            return 0;
        }

        e->text_lines[e->line_number][e->char_number - 1] = '\0';

        if(e->char_number == 0 && e->line_number > 0){
            e->line_number -= 1;
            has_line_changed = 1;
            e->char_number = strlen(e->text_lines[e->line_number]);
        }else if(e->char_number == 0 && e->line_number == 0){
            ;
        }else{
            e->char_number -= 1;
            e->actual_char_counts[e->line_number] -= 1;
        }

        return 0;

    }else if(e->c == CTRL_T){ // Switches between insert and overtype modes

        overtype_mode = overtype_mode ^ 1;
        input_mode_change_flag = 1;
        return 0;

    }else if(e->c == 91 /*[*/ && e->prev_c == '\e' /*ESC*/){ // Checking for CSI (Control Sequence Introducer)

        e->is_CSI = 1;
        return 0;

    }else if(e->is_CSI){ // Control Sequence Introduced

        arrow_key_handling(e);

        e->is_CSI = 0;
        return 0;

    }else if(e->c < 32){ // Skipping other undefined non-printable characters
        return 0;
    }

    if(!overtype_mode){ // In case of insert input/typing mode.
        e->actual_char_counts[e->line_number] += 1;
        for(int i = e->actual_char_counts[e->line_number]; i > e->char_number; i--){
            e->text_lines[e->line_number][i] = e->text_lines[e->line_number][i-1];
        }
    }
    
    e->text_lines[e->line_number][e->char_number] = e->c; // assigning new char

    if(e->actual_char_counts[e->line_number] >= e->allocated_char_counts[e->line_number] - 1){// Safety measurements
        
        grow_curr_line_len(e);

    }
    
    e->char_number += 1;
    if(e->char_number > e->actual_char_counts[e->line_number] && overtype_mode) // In case of overtype mode (to not repat for insert input mode)
        e->actual_char_counts[e->line_number] += 1;
    
    has_line_changed = 0;

    return 0;
}

int print_logic(struct winsize *ws, struct editor_state *e){ // The editor's main printing/render logic

    if(window_resized == 1){
        get_window_size(ws);
        if(DEBUG_MODE){
            printf("Window size has changed!\r\n");
            window_resized = 0;
        }
    }

    if(DEBUG_MODE){}
    else{
        
        printf(HIDE_CURSOR);
        //Place cursor on the second to last line of terminal screen (status bar).
        CURSOR_MOVE_ROW(ws->ws_row-1);
        if(file_saved_flag){

            printf("---\r\nFile \"%s\" saved successfully!", e->curr_path);
            file_saved_flag = 0;

        }else if(file_opened_flag){

            printf("---\r\nFile \"%s\" opened successfully!", e->curr_path);
            file_opened_flag = 0;
            
        }else if(file_open_error_flag){

            printf("---\r\nFile \"%s\" not found or unable to open!", e->curr_path);
            file_open_error_flag = 0;

        }else if(input_mode_change_flag){
            
            printf(REFRESH_BELOW_CURSOR);
            if(overtype_mode)
                printf("---\r\nTyping mode changed (overwrite)");
            else
                printf("---\r\nTyping mode changed (insert)");
            input_mode_change_flag = 0;

        }else{
            if(ws->ws_col >= strlen(STATUS_BAR_TEXT_LONG) - 12*5) //12 is the number of special characters used for formatting (invis) all the "CTRL+" commands
                printf(STATUS_BAR_TEXT_LONG);
            else if(ws->ws_col >= strlen(STATUS_BAR_TEXT) - 12*5)
                printf(STATUS_BAR_TEXT);
            else
                printf(STATUS_BAR_TEXT_SHORT);

        }

        if(window_resized){
            return_to_editor_screen = 1;
            window_resized = 0;
        }

        // Checks if screen has scrolled and if yes, refreshes whole editor's text space and prints whole text again
        int screen_scrolled = 0; 

        //Printing proper editor's main text:
        if(e->upper_screen_bound == e->line_number){
            if(e->line_number != 0){
                e->upper_screen_bound -= 1;
                screen_scrolled = 1;
            }
        }else if(e->line_number - e->upper_screen_bound > ws->ws_row-3){
            e->upper_screen_bound += 1;
            screen_scrolled = 1;
        }
        
        if(screen_scrolled || has_line_changed || return_to_editor_screen){

            printf(REFRESH_ABOVE_STATUS_BAR);

            int limit = e->line_count - e->upper_screen_bound;
            if(limit > ws->ws_row-2)
                limit = ws->ws_row-2;
            for(int i = e->upper_screen_bound; i < e->upper_screen_bound+limit; i++)
                printf("%s\r\n", e->text_lines[i]);

            screen_scrolled = 0;
            return_to_editor_screen = 0;

        }else{
            printf("\e[%d;1H", e->line_number - e->upper_screen_bound + 1);
            printf(REFRESH_ENTIRE_LINE);
            printf("%s", e->text_lines[e->line_number]);
            //v Make it work later !
            /*printf("\e[%d;%dH", line_number - *upper_screen_bound + 1, char_number + 1);
            printf(REFRESH_TIL_LINE_END);
            for(int i = char_number; i < actual_char_counts[line_number]; i++){
                putchar(text_lines[line_number][i]);
            }*/
        }

        // Place cursor at appropriate line/column
        //printf("\e[%d;%dH", line_number + 1, char_number + 1); // Name or macro it in more sensible way later
        // More relative/dynamic version of the above:
        printf("\e[%d;%dH", e->line_number - e->upper_screen_bound + 1, e->char_number + 1);

        printf(SHOW_CURSOR);
    }

    return 0;
}

// Allocates and initialized memory for text data.
int alloc_mem_for_text(struct editor_state *e){ 

    e->text_lines = malloc(e->line_count * sizeof(char*));
    e->allocated_char_counts = malloc(e->line_count * sizeof(int)); // dynamic table of memory allocated for number of space allocated for characters in text lines.
    e->actual_char_counts = malloc(e->line_count * sizeof(int)); // dynamic table of memory allocated for actual number of characters in text lines.
    // Think whether the data structure wouldn't be a better idea later
    if (!(e->text_lines) || !(e->allocated_char_counts) || !(e->actual_char_counts)) {
        perror("malloc failed");
        return -1;
    }
    for(int i = 0; i < e->line_count; i++){
        e->text_lines[i] = calloc(STARTING_LINE_LEN, sizeof(char)); //do check for calloc later.
        e->allocated_char_counts[i] = STARTING_LINE_LEN;
        e->actual_char_counts[i] = 0;
    }

    return 0;
}

int free_text_mem(struct editor_state *e){

    // Freeing text_lines container contents
    for(int i = 0; i < e->line_count; i++){
        free(e->text_lines[i]);
    }
    free(e->text_lines);
    // Freeing allocated_char_counts contents
    free(e->allocated_char_counts);
    // Freeing actual_char_counts contents.
    free(e->actual_char_counts);

    return 0;
}

int main(void) {

    signal(SIGWINCH, handle_sigwinch); // Signal (works/is run asynchronously) in case terminal's window size changes.

    enable_raw_mode();

    printf(FULL_SCREEN_REFRESH); // Initial screen refresh

    struct editor_state *e = malloc(sizeof(struct editor_state));

    e->line_count = STARTING_TEXT_LINES;
    e->line_number = 0;
    e->char_number = 0;

    alloc_mem_for_text(e);

    e->curr_path[0] = '\0';
    setbuf(stdout, NULL);
    struct winsize *ws = malloc(sizeof(struct winsize));
    get_window_size(ws);

    e->prev_c = '\0'; // Stores previous character in order to check for escape sequences.
    e->is_CSI = 0; // is Control Sequence Introducer (ANSI)
    //Is true (1), when control sequence introducer was present i.e: characters: '\e' followed by: '[' and then control sequence.
    
    // First (upper-most) line that is visible on the editor's screen/window.
    e->upper_screen_bound = 0;

    while (1) {

        print_logic(ws, e);

        if (read(tty_fd, &(e->c), 1) == 1) {

            if(DEBUG_MODE){

                printf("You pressed: %c %d\r\n", e->c, e->c);
                printf("Window size:\r\nrows: %d, cols: %d\r\n", ws->ws_row, ws->ws_col);
                if(e->c == 'q') break;
                printf("===================\r\n");

            }else{

                if(key_handling(e, 0) == -1) // Normally doesn't return -1, so if that's the case then:
                    break; //Exits the program
                
                e->prev_c = e->c;
                
            }
        }
    }

    if(DEBUG_MODE){}
    else{
        printf(FULL_SCREEN_REFRESH);
        printf("Quitting the editor...\r\n");
    }

    // Freeing window size data structure
    free(ws);
    free_text_mem(e);
    return 0;
}
