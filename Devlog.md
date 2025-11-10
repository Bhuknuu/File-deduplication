alot of work done, uploading 
==============================
Phase 3 day 1 (05-11-25)
didn't worked in project, collected the team and segregated the work
==============================


# First, check your actual filename
ls *.c

# Then compile with the CORRECT filename (change Traversal to traversal if needed)
gcc -c main.c `pkg-config --cflags gtk+-3.0`
gcc -c gui.c `pkg-config --cflags gtk+-3.0`
gcc -c Traversal.c `pkg-config --cflags gtk+-3.0`
gcc -c filter.c `pkg-config --cflags gtk+-3.0`
gcc -c report.c `pkg-config --cflags gtk+-3.0`
gcc -c action.c `pkg-config --cflags gtk+-3.0`

# Link
gcc main.o gui.o Traversal.o filter.o report.o action.o -o file_dedup.exe `pkg-config --libs gtk+-3.0`

# Run
./file_dedup.exe