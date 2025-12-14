# Dirmark

This is a tool similar to quickfix, but it allows editing, the idea is to load a file ($XDG_CONFIG/hed/dirmark.txt)
that will have a new line separated list of directories.

When the cursor is under directory and " cd" is typed the pwd is changed to that directory
dirmark buffer can open other files that contain a list of directories and can navigate through them with 
" mn" -> next directory
" mp" -> previous directory
" ma" -> add current directory in the markdir
" mt" -> toggle the view of the markdir buffer

In the buffer it self i should be able to change the text and add new things

