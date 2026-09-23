# Emojicode [![CI](https://github.com/emojicode/emojicode/actions/workflows/ci.yml/badge.svg)](https://github.com/emojicode/emojicode/actions/workflows/ci.yml) [![Join the chat at https://gitter.im/emojicode/emojicode][image-2]][2]

Emojicode is an open source, high-level, multi-paradigm
programming language consisting of emojis. It features Object-Orientation, Optionals, Generics and Closures.

## 🏁 Getting Started

**To learn more about the language and how to install Emojicode visit https://www.emojicode.org/.**

We highly recommend to follow Emojicode’s Twitter account [@Real\_Emojicode][6] to stay up with the latest.

## 🔨 Building from source

### 🏡 Building locally

Prerequisites (versions are recommendations):

- A C++17 compiler, e.g. clang 18+ or gcc 13+
- CMake 3.20+ and (preferably) Ninja
- LLVM 18 or newer (tested with LLVM 18 and 20)
- Python 3.8+ for testing

On Ubuntu 24.04 these can be installed with:

```sh
sudo apt install clang-20 llvm-20-dev cmake ninja-build python3 rsync zlib1g-dev libzstd-dev
```

Steps:

1. Clone Emojicode (or download the source code and extract it) and navigate
  into it:

   ```sh
   git clone https://github.com/emojicode/emojicode
   cd emojicode
   ```

2. Create a `build` directory and run CMake in it:

   ```sh
   mkdir build
   cd build
   cmake .. -GNinja
   ```

   If CMake does not pick up the right LLVM version, point it to LLVM’s CMake
   directory, e.g. `-DLLVM_DIR=/usr/lib/llvm-20/lib/cmake/llvm`.

   You can of course also run CMake in another directory or use another build
   system than Ninja. Refer to the CMake documentation for more information.

3. Build the Compiler and Packages:

   ```sh
   ninja
   ```

4. You can now test Emojicode:

   ```sh
   ninja tests
   ```

5. The binaries are ready for use!
   You can the perform a magic installation right away

   ```sh
   ninja magicinstall
   ```

   or just package the binaries and headers properly

   ```sh
   ninja dist
   ```

   To create a distribution archive you must call the dist script yourself
   (e.g. `python3 ../dist.py .. archive`).

### 🐋 Building using Docker

A `Dockerfile` is available for building in a Ubuntu `24.04` environment.

Steps:

1. Clone Emojicode (or download the source code and extract it) and navigate
  into it:

   ```sh
   git clone https://github.com/emojicode/emojicode
   cd emojicode
   ```

2. Build Docker image:

   ```sh
   docker build -t emojicode-build -f docker/clang .
   ```

3. Verify the installation was fine and tests pass:

   ```
   docker run --rm emojicode-build
   ...
   ✅ ✅  All tests passed.
   ```

4. Start image (and mount a directory to it):

   ```sh
   docker run --rm -v $(pwd)/code:/workspace -it emojicode-build /bin/bash
   ```

5. Start coding!

   ```sh
   emojicodec /workspace/hello.🍇 && ./workspace/hello
   ```

## 📃 License

Emojicode [is licensed under the Artistic License 2.0][8].
If you don’t want to read the whole license, here’s a summary without legal force:

- You are allowed to download, use, copy, publish and distribute Emojicode.
- You are allowed to create modified versions of Emojicode but you may only distribute them on some conditions.
-  The license contains a grant of patent rights and does not allow you to use any trademark, service mark, tradename, or logo.
- Emojicode comes with absolutely no warranty.

[2]:	https://gitter.im/emojicode/emojicode?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge
[6]:	https://twitter.com/Real_Emojicode
[7]:	https://github.com/emojicode/emojicode/blob/master/0.6.md#help-improving-emojicodes-syntax-
[8]:	LICENSE

[image-1]:	https://app.codeship.com/projects/edbc3220-f394-0134-fad2-66135ababc06/status?branch=master
[image-2]:	https://badges.gitter.im/emojicode/emojicode.svg
