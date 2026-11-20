#pragma once

#include <cstdlib>
#include <iostream>

#define PC_CHECK(cond)                                                              \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__                   \
                      << " condition failed: " #cond "\n";                          \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)
