#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct{
	char **words;
	int size;
} Dictionary;

/* Returns 0 if and only if the board is in a valid board state.
 * Otherwise returns 1.
 * 
 * A valid row contains same amount of letters with no duplicate letters  
 * the column is the number of letters of the board, 
 * where it is invalid when it is less than 3 sieds.
 * 
 * board: heap allocated 2D array of char
 * sides:  number of columns in the board
 * letters: number of rows in the board
 */

int valid_board(char **board, int sides, int letters) {
    if (sides < 3) {
        printf("Invalid board\n");
        return 1;
    }

    for (int i = 0; i < sides; i++) {

        // Check for incorrect number of letters
        if ((int)strlen(board[i]) != letters) {
            printf("Invalid board\n");
            return 1;
        }

        for (int j = 0; j < letters; j++) {
            char character = board[i][j];
            // Check for duplicate letters in the current row
            for (int column = j + 1; column < letters; column++) {
                if (character == board[i][column]) {
                    printf("Invalid board\n");
                    return 1;
                }
            }
            // Check for duplicate letters in the current column
            for (int row = i + 1; row < sides; row++) {
                if (character == board[row][j]) {
                    printf("Invalid board\n");
                    return 1;
                }
            }
        }
    }
    return 0;
}


/*
 *remove a new line
 */

void remove_newline(char *line) {
    int str_len = (int)strlen(line);
    if (str_len > 0 && line[str_len - 1] == '\n') {
        line[str_len - 1] = '\0';
    }
}

/* 
 * Read dictionary into a dynamically sized array of strings
 */
Dictionary *read_dictionary(FILE *fp_dict){
	Dictionary *dict = malloc(sizeof(Dictionary));
	if (dict == NULL){
		printf("Memory allocation failed for dictionary\n");
		return NULL;
	}

	dict->words = NULL;
	dict->size =0;
	
	char *line = NULL;
	size_t len = 0;
	while(getline(&line, &len, fp_dict) != -1){
			remove_newline(line);
			//dynamically resize the array dict->words to accommodate an additional
			//word every time a new word is read from the dictionary file.	
			dict->words = realloc(dict->words, (dict->size + 1) * sizeof(char*));
			if (dict->words == NULL){
				printf("Memory reallocation failed for dictionary words\n");
				free(line);
				for (int i = 0; i < dict->size; i++) {
					free(dict->words[i]);
				}
				free(dict);
				return NULL;
			} 
			
			//dynamically allocate the rest word in the dictionary
			dict->words[dict->size] = malloc(strlen(line) + 1);
			if (dict->words[dict->size] == NULL){
				printf("Memory reallocation failed for the rest of the words in dictionarys\n");
				free(line);
				for (int i = 0; i < dict->size; i++) {
					free(dict->words[i]);
				}
				free(dict->words);
				free(dict);
				return NULL;
			}
			strcpy(dict->words[dict->size], line);
			dict->size++;
	}
	free(line);
	return dict;
}

/*
 * check if the word is in the dictionary
 */
int is_word_in_dictionary(Dictionary *dict, char *word){
	for(int i = 0; i < dict->size; i++){
		if(strcmp(dict->words[i], word) == 0){
			return 1;
		}
	}
	return 0;
}

/*
 * check if the word is formed by different sides of board 
 * or using letters on the same side consecutively
 */
int check_consecutive_letters(char *word, char **board, int sides, int *last_side){
	(void)word;
	int used_sides_flag[sides];
	//initializes the entire used_sides array to zero,
	for (int i = 0; i < sides; i++){
		used_sides_flag[i] = 0;
	}
	for (char *pt = word; *pt != '\0'; pt++){
		for (int i = 0; i < sides; i++){
			// Check if the current letter *p is in the side board[i]
			if(strchr(*(board + i), *pt)){
				// Check if the current side is the same as the last used side
				if(used_sides_flag[i] == 1 && i == *last_side){
					printf("Same-side letter used consecutively\n");
					return 0; //same side is used consecutively, so it's invalid
				} 
				used_sides_flag[i] = 1; // mark the current side is used
				*last_side = i;
				break;
			}
		}
	}	
	return 1;
}

/*
 * Check if the word can be formed with the board's letters
 */
int check_letter_on_board(char *word, char **board, int sides) {
    for (char *pt = word; *pt; pt++) {
        int found = 0;
        for (int i = 0; i < sides; i++) {
            if (strchr(board[i], *pt)) {
                found = 1;
                break;
            }
        }
        if (!found) {
            printf("Used a letter not present on the board\n");
            return 0;
        }
    }
    return 1;
}

/*
 * check if the solution is valid
 */
int solution(char **board, int sides, Dictionary *dict) {
    char *word = NULL; 
    size_t len = 0;
    ssize_t read;
    char last_used_char = '\0';
    int last_side = -1;
    int board_letters[26] = {0};// Array to keep track of letters present on the board
    int used_letters[26] = {0};// Array to keep track of used letters

	// Mark letters present on the board
    for (int i = 0; i < sides; i++) {
        for (int j = 0; j < (int)strlen(board[i]); j++) {
            if (board[i][j] >= 'a' && board[i][j] <= 'z') {
                board_letters[board[i][j] - 'a'] = 1;
            }
        }
    }

    // Read words from stdin until EOF
    while ((read = getline(&word, &len, stdin)) != -1) {
        remove_newline(word);

        if (!is_word_in_dictionary(dict, word)) {
            printf("Word not found in dictionary\n");
            free(word); 
            return 0;
        }

        // Check if the first letter of the current word matches the last letter of the previous word
        if ((last_used_char != '\0') && (word[0] != last_used_char)) {
            printf("First letter of word does not match last letter of previous word\n");
            free(word); 
            return 0;
        }

        // Check consecutive letters from different sides
        if (!check_consecutive_letters(word, board, sides, &last_side)) {
            free(word); 
            return 0;
        }

        // Check if all letters in the word are on the board
        if (!check_letter_on_board(word, board, sides)) {
            free(word); 
            return 0;
        }

        // Mark all used letters in the word
        for (int i = 0; word[i]; i++) {
            if (word[i] >= 'a' && word[i] <= 'z') {
                used_letters[word[i] - 'a'] = 1;
            }
        }        

        last_used_char = word[strlen(word) - 1]; // Update last used char
    }

    // Check if all letters on the board were used
    for (int i = 0; i < 26; i++) {
        if ((board_letters[i] == 1) && (used_letters[i] != 1)) {
            printf("Not all letters used\n");
            free(word); 
            return 0;
        }
    }
    free(word); 
    printf("Correct\n");
    return 0;
}

int main(int argc, char *argv[]){
	if(argc != 3){
		printf("Usage: %s <board_file> <dict_file>\n", argv[0]);
		exit(1);
	}

	// Open the file 
	FILE *fp_board = fopen(argv[1],"r");
	FILE *fp_dict = fopen(argv[2], "r");
	if (fp_board == NULL){
		printf("Unable to open board file");
		exit(1);
	}
	if (fp_dict == NULL){
		printf("Unable to open dictionary file");
		exit(1);
	}
	
	int sides = 0;//board's row
	int letters = 0;//board's column
	char*line = NULL;
	size_t len = 0;

	//read the board file to determine the sides
	while (getline(&line, &len, fp_board) != -1){
		sides++;
	}

	if (sides == 0) {
        printf("Board file is empty\n");
        fclose(fp_board);
        fclose(fp_dict);
		free(line);
        exit(1);
    }
	rewind(fp_board);

	//read each line determine how many characters each sides has
	if (getline(&line, &len,fp_board) != -1){
		letters = (int)strlen(line) - 1;// have to remove the newline character
		if (letters < 1) {
			printf("Board file has invalid line\n");
			fclose(fp_board);
            fclose(fp_dict);
			free(line);
            exit(1);
		} 
	} else {
		printf("Error reading the first line of the board file\n");
        fclose(fp_board);
        fclose(fp_dict);
		free(line);
        exit(1);
    }
	rewind(fp_board);

	//allocate memory for the board
	char **board = malloc(sides * sizeof(char *));
	if (board == NULL){
		printf("%s\n","Memory allocation failed for board rows");
		free(line);
		exit(1);
	}

	for (int i = 0; i < sides; i++){
		*(board + i) = malloc(letters * sizeof(char));
		if(*(board + i) == NULL){
			printf("%s\n","Memory allocation failed for board columns");
			for (int j = 0; j < i; j++){
				free(*(board + j));
			}
			free(board);
			free(line);
			exit(1);
		}
	}

	//read the remianing line in board file
	for(int i = 0; i < sides; i++){
		if(getline(&line, &len, fp_board) == -1){
			printf("Error while reading line %i of the file.\n", i + 1);
			free(line);
			for (int j = 0; j < sides; j++) {
                free(*(board + j));
            }
			free(board);
			exit(1);
		}
		strncpy(*(board+i), line, letters);
		board[i][letters] = '\0'; //remove the last newline
	}
		
	//Call valid_board and print the appropriate
	//output depending on the function's return value.
	if (valid_board(board, sides, letters) != 0){
		free(line); 
        for (int i = 0; i < sides; i++) {
            free(*(board + i));
        }
        free(board);
		exit(1);
	}

	// Read the dictionary
	Dictionary *dict = read_dictionary(fp_dict);
    if (!dict) {
        printf("Error loading dictionary\n");
		free(line);  \
        for (int i = 0; i < sides; i++) {
            free(*(board + i));
        }
        free(board);
        exit(1);
    }
    
    //check the solution
	int result = solution(board, sides, dict);
	//Close the file
	if (fclose(fp_board) != 0) {
		printf("Error while closing the board file.\n");
		free(line);
		exit(1);
	} 
	if (fclose(fp_dict) != 0) {
		printf("Error while closing the dictionay file.\n");
		free(line);
		exit(1);
	}

	for(int i = 0; i < sides; i++){
		free(*(board + i));
	}
	free(board);
	
	for (int i = 0; i < dict->size; i++) {
        free(dict->words[i]);
    }
	free(line);
    free(dict->words);
    free(dict);
	
	return result; 		
}

