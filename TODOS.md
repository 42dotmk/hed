# TODOS

## [TODO] LSP Automation
We have to make it so we have a list of lsps and then we can have commands for starting and stopping them
there might be a need for multiple instances of the same lsp in some cases as well.

## [TODO] Virtual text :prio-high:
We need this ability so we can display virtual text so we can:
- autocomplete
- show linting messages/references or similar.
Investigate how big the change will be in the rendering layer.
The rest should not be affected because we will only render more. still the virtual text can support multiple lines so that can be an issue as well with the navigation. 

## [TODO] Autocomplete utilities
Extend the modals API, and provide this as plugins

## [TODO] Alternative to FZF :low:
   Use local GUI for searching and filtering(more control for the UI).

## [TODO] Themes :low:
   At the moment only one theme is supported, we should allow switching themes, this may be a plugin, but will also require change in how the theme is applied.
   Note that the themes in hed rely on the terminal background, there is no plan to support custom backgrounds in the future.

## [TODO] Better integration with AI
   Copilot, ClaudeCode, Opencode should be able to interact from the editor.

## [IN-PROGRESS] Multicursor support
   Not there yet but we are going somewhere

## [DONE] Ability to communicate with hed through named pipes or sockects
   Investigate best approach on how to send commands to hed through sockets or pipes. this will allow us to be able to communicate commands through other applications.
   Recommend what is best approach to do this, also i would like to do it in a form of a plugin, what are the implications of the plugin case, do we need to addapt something in the core in order to support this?
   Investigate and let me know.
## [DONE] Better window resizing
   Add keybindings
   
## [DONE] Scratch pane plugin  

## [DONE] Persisting open buffers on reload
on :reload i would like to reopen the files that were opened
Keeping track of the list of open buffers at the current directory
Ideation

## [DONE] Imports sanitization
   dont use hed.h internally, it is intended for plugin development only.

## [DONE] Replace vector.h/c with stb_ds.h
No need to maintain this, stb_ds is much better and has more funcitonality already

## [DONE] Tmux pluigin send selection
Ability to  send visual selection to an active tmux pane
If there is no new line sent we should esure to send new line at the end , so we ensure that the command is executed in the tmux pane.

## [DONE] Better API for the command mode
   Its not good at the moment, at all

