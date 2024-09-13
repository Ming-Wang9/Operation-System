Name: Ming Wang
cs login: mingw
Email: mwang583@wisc.edu
wiscID: 908 494 5329

# Lettter Boxed Solver


## Introduction

This project solves the Letter Boxed game, it can be found at New York Times puzzle https://www.nytimes.com/
The board is divided into an arbitraty sides,each sides has teh same amount of letters, 
players need to form a word using letters from those sides, it can starts with any letter on the board.
If a letter is present on one side of the board, it cannot be present on other sides,
means there is no duplicate letters on the board.
But the first character of next word have to match the last character of its previous word.
Letters can be used more than once, but can't not be used consecutively

##Function

- `valid_board`: check the board is valid, it has more than 3 sides, no duplicate letters on the board
- `read_dictinary`: load the dictinary file, and allocate it into memory
- `is_word_in_dictionary`: check if the formed word is in dict.txt file
- `check_consecutive_letters`: no letter can be used more than once consecutively
- `check_letter_on_board`: it should use the letter that is on the board
- `solution`: check the letter on the board is being used at least onece, and print correct if the board is solved

##Files

- `letter-boxed.c`: The C source file for the Letter Boxed solver.
- `board.txt`: board file with the board's configuration, can have different name base on tests
- `dict.txt`: dictionary file with valid words, can have different name base on tests

## Compilation

To compile the program, use the following command:
gcc letter-boxed.c -std=c17 -O2 -Wall -Wextra -Werror -pedantic -o letter-boxed

##Usage

./letter-boxed <board_file> <dict_file>

##Citation

Following Links has provided certian to this project

pointer in C: https://www.geeksforgeeks.org/c-file-pointer/
count the lines in a file and reset the point to the start: https://stackoverflow.com/questions/32366665/resetting-pointer-to-the-start-of-file
linux manual page: https://man7.org/linux/man-pages/man3/getline.3.html
