// Global module fragment where #includes can happen
module;
#include <iostream>

// first thing after the Global module fragment must be a module command
export module foo;

export class foo {
public:
  void hello() {
    std::cout << "Hello world!" << std::endl;
  }
};
