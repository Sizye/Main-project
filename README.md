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
  ./lexer [input.txt] [output.txt]
    ```
Then write any string from I toy programming language and program will return program in tokens.

# ALL PULL REQUESTS IN MAIN MUST COMPLETE PARSING OF ULTIMATE_TEST.txt OR REPORT ACTUAL ERROR IN TEST ITSELF.
