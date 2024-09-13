---
title: CS 537 Project 1
layout: default
---

# CS537 Fall 2024, Project 1

## Updates
* TBD

## Administrivia 
- **Due Date** by September 13, 2024 at 11:59 PM
- Questions: We will be using Piazza for all questions.
- Collaboration: The assignment has to be done by yourself. Copying code (from others) is considered cheating. [Read this](http://pages.cs.wisc.edu/~remzi/Classes/537/Spring2018/dontcheat.html) for more info on what is OK and what is not. Please help us all have a good semester by not doing this.
- This project is to be done on the [lab machines](https://csl.cs.wisc.edu/docs/csl/2012-08-16-instructional-facilities/), so you can learn more about programming in C on a typical UNIX-based platform (Linux).
- A few sample tests are provided in the project repository. To run them, execute `run-tests.sh` in the `tests/` directory. Try `run-tests.sh -h` to learn more about the testing script. Note these test cases are not complete, and you are encouraged to create more on your own.
- Handing it in: Copy the whole project, including solution and tests folder, to `~cs537-1/handin/login/p1` where login is your CS login.

## Letter Boxed
In this assignment, you will write a program which checks solutions to the word game Letter Boxed.

Learning Objectives:

* Re-familiarize yourself with the C programming language
* Familiarize yourself with a shell / terminal / command-line of Linux
* Learn about file I/O, string processing, and simple data structures in C

Summary of what gets turned in:
* One `.c` file: `letter-boxed.c`
* It is mandatory to compile your code with following flags `-std=c17 -O2 -Wall -Wextra -Werror -pedantic -o letter-boxed`.
    * Check `man gcc` and search for these flags to understand what they do and why you will like them.
    * It is a good idea to create a `Makefile` so you just type `make` for the compilation.
    * We are trying to use the most recent version of the C language. However, the compiler (`gcc` on lab machines) does not support final C23 specification, which is the most recent one. So we are using the second most recent (C17).
* It should (hopefully) pass the tests we supply. 
* Include a single `README.md` describing the implementation. This file should include your name, your cs login, you wisc ID and email, and the status of your implementation. If it all works then just say that. If there are things you know doesn't work let me know.

__Before beginning__: Read this [lab tutorial](http://pages.cs.wisc.edu/~remzi/OSTEP/lab-tutorial.pdf); it has some useful tips for programming in the C environment.

## letter-boxed
The program you will build is called `letter-boxed`. This program plays the New York Times puzzle [Letter Boxed](https://www.nytimes.com/puzzles/letter-boxed). It takes two command line arguments -- a board file and a dictionary file. Then, it reads words from standard input (`STDIN`) until the board is solved, or until an invalid solution is attempted.

Here is what it looks like to run the program:

```bash
$ ./letter-boxed board1.txt dict.txt
```

## Rules of the game

The rules of letter boxed are simple. Given a letter boxed board, you must use each letter in the board at least once to form words found in the dictionary. You can start on any letter, but the first character of each subsequent word must be the same as the last character of the previous word. Letters on the same side of the board may not be used consecutively. Letters can be used more than once, even in a single word. Letters not present on the board must not be used. If a letter is present on one side of the board, it cannot be present on other sides, i.e. just a single occurrence of a letter on the board.

For simplicity, we will only use lower-case ASCII characters `a-z`. Thus, there are 26 characters in the entire alphabet in use for this project. This is true for the boards, dictionary, and solution inputs. 

Let us look at an example:

```
    r  o  k
  +---------+
  |         |
 w|         |e
  |         |
 f|         |d
  |         |
 a|         |n
  |         |
  +---------+
    l  c  i

```

A solution to this board could be
```
flan
now
wreck
kid
```

However, `wrecked` would be an invalid word because the `e` and `d` are on the same side of the board. The same solution with the words in a different order would also be invalid, because that would break the rule of consecutive words sharing their last/first letters.

Since we are writing a programmatic solver, we can be more flexible with the game. Boards might have more than four sides (although never less than three), and sides need not always have three letters.

## File formats
The picture above is suitable for humans playing the game. Our board files are a little simpler -- a board file has one line of text per side of the board.

```bash
$ cat board1.txt
rok
edn
lci
wfa
```

The dictionary file will have one word per line.

You can expect the solution to be input through standard input (`STDIN`) as one word per line. The program should run until an invalid board is detected, an input error is found, the board is solved, or end of file (`EOF`) is reached.

## Possible outputs
- If the board is invalid (less than 3 sides), print `Invalid board\n` and exit with return code 1.
- If a board contains a letter more than once, print `Invalid board\n` and exit with return code 1.
- If the solution uses a letter not present on the board, print `Used a letter not present on the board\n` and exit with return code 0.
- If the solution does not use all the letters on the board, print `Not all letters used\n` and exit with return code 0.
- If the solution illegally uses letters on the same side of the board consecutively, print `Same-side letter used consecutively\n` and exit with return code 0.
- If the solution uses a word not found in the dictionary, print `Word not found in dictionary\n`, and exit with return code 0.
- If the first character of a word in the solution is not the same as the last character in the preceding word, print `First letter of word does not match last letter of previous word\n` and exit with return code 0.
- If the solution is correct, print `Correct\n`, and exit with return code 0.
- All other errors (wrong number of arguments, open file failed, etc.) should exit with code 1.

## Tips
- When working with any C library functions, check their manual page (`man`) for a description and proper usage, e.g. `man 3 fopen`.
- To work with files, look at `man 3 fopen` and `man 3 fclose`.
- To read data from files and STDIN, consider using `getline(3)`, `fgets(3)`, or maybe `scanf(3)`.
- Printing to the terminal can be done with `printf(3)`.
- You don't know the size of boards, words, or the dictionary ahead of time, so you will need to dynamically allocate memory.
- You will need to compare words against the dictionary. We aren't evaluating your program based on performance, so you could use something as simple as a linked list and linear search, but feel free to explore faster solutions like a dynamically-sized array with binary search, or tries. 

## Acknowledgments
Assignment written by John Shawger, Letter Boxed may be found at www.nytimes.com
