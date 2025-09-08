# Main-project
### Technology stack
- C++ (compiler language)
- Flex
- Bison

### Steps to compile lexer
* Run lex.yy.cc file generation using flex:
  ```bash
   flex lexer.l
   ```

* Compile lex.yy.cc file using g++
  ```bash
  g++ lex.yy.cc -o lexer
    ```
* Finally execute object file
  ```bash
  ./lexer
    ```
Then write any string from I toy programming language and program will return program in tokens.
