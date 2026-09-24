//
// Created by Theo Weidmann on 17.03.18.
//

#ifndef EMOJICODE_INTERNAL_HPP
#define EMOJICODE_INTERNAL_HPP

#include <atomic>

namespace runtime {

/// This namespace contains variables that should not be considered part of the public API
namespace internal {

extern int argc;
extern char **argv;
extern int seed;

struct ControlBlock {
    std::atomic_int strongCount{1};
    /// The number of weak references plus one, which is held collectively by all strong references and is given up
    /// once the strong count has reached zero. Whoever takes this count to zero deletes the control block.
    std::atomic_int weakCount{1};
};

struct Capture {
    ControlBlock *controlBlock;
    void (*deinit)(Capture*);
};

}

}

#endif //EMOJICODE_INTERNAL_HPP
