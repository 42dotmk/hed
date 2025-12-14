# CTags implementation

I would like to use a generated tags file to implement a goto definition

a tags file is line based, it is generated through the ctags -R command
its format is 

TAG  FILEPATH  REGEX_STRING 

REGEX_STRING is the regext to locate the place where the tag is defined

Please build a module that is implementing:
 find_tag
 goto_tag
 tag_exists

It should use rg to search for the tag line
and then parse the line, open the file buffer and apply search with the given regex

Try to keep it simple, we have all the tools needed
  

