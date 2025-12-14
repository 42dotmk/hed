# Macro system

The idea of the macro system is to be able to bypass (mimic) the keyboard inputs and let the editor handle them as if they were typed by the user.

In a pay we should be able to run a sertain sequence of characters and it needs to be executed as if they were coming from the keyboard.

This also includes esc and special characters as well.
Take a note that these ('macro') keypresses can be applied in any mode and also maybe some of the keybindings will also require additional keypress arguments by itself.

So I need a solution that will work without the underlying functions are aware that this is a macro and not real user input.
