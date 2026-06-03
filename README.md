Open‑Lotto

A modular, high‑entropy lottery number generator written in C.

Open‑Lotto is a plugin‑based lottery engine designed with clean architecture, strong randomness guarantees, and extensibility in mind. It uses a hybrid entropy source, a PCG32 random generator, and dynamically loaded plugins to simulate real lottery draws with accuracy and style.

🚀 Features

🔐 Hybrid RNG Seeding

Every draw uses a high‑quality seed built from:

Linux kernel entropy (getrandom())

Hardware entropy (RDRAND) when supported

Monotonic clock jitter

These sources are XOR‑combined to produce a robust, unpredictable seed for each draw.

🎲 PCG32 Random Number Generator

Open‑Lotto uses PCG32, a modern, fast, statistically strong RNG suitable for simulations and Monte‑Carlo‑style workloads.

🔌 Plugin‑Based Game System

Each lottery game is implemented as a shared library (.so) that defines:

Main number range

Extra number range (if any)

Number of picks

Game name

Plugins are discovered automatically at runtime.

🎞 Animated Draw Mode

Enable --animate to watch numbers roll in with a smooth terminal spinner animation.

📦 Multiple Draws

Generate any number of independent draws:

./open-lotto --game lotto --draws 10

📦 Build Instructions

mkdir build
cd build
cmake ..
make -j

This produces:

open-lotto
plugins/
    liblotto.so
    libeurojackpot.so

🕹 Usage

List available games

./open-lotto --list-games

Run a game

./open-lotto --game lotto

Animated draw

./open-lotto --game eurojackpot --animate

Multiple draws

./open-lotto --game lotto --draws 7

Combine options

./open-lotto --game lotto --animate --draws 5

🧩 Plugin Architecture

Each plugin must implement two functions:

const LotteryInfo* lottery_get_info(void);
void lottery_generate(LotteryResult *out, draw_event_callback cb);

Plugins are compiled as shared libraries and placed in:

```
build/plugins/
```

The loader extracts the game name from the plugin metadata, not from the .so filename.

Example plugin structure
```
plugins/
├── lotto.c
└── eurojackpot.c
```

🔧 RNG Architecture

Hybrid Seed Generation

The seed is built from:

getrandom() (primary entropy)

RDRAND (if CPU supports it)

CLOCK_MONOTONIC timestamp

Random Number Generation

Open‑Lotto uses:

PCG32 for main RNG

Fisher–Yates shuffle for pool randomization

This combination ensures:

High entropy

Uniform distribution

No repetition within a draw

Fast performance

📁 Project Structure
```
open-lotto/
├── include/
│   ├── combogen.h
│   ├── lottery_plugin.h
│   ├── plugin_loader.h
│   ├── random.h
│   └── log.h
├── src/
│   ├── combogen.c
│   ├── plugin_loader.c
│   ├── random.c
│   ├── log.c
│   └── main.c
├── plugins/
│   ├── lotto.c
│   └── eurojackpot.c
├── CMakeLists.txt
└── README.md
```

📜 License

MIT License.

👤 Author

WissemEmbedded Linux Developer & Firmware ArchitectSaxony, Germany

🤝 Contributions

Contributions, new lottery plugins, and improvements are welcome!

**Getting started?** See [CONTRIBUTING.md](CONTRIBUTING.md) for:
- How to build locally
- How to write new lottery plugins
- Code style and testing requirements
- How to submit pull requests

Feel free to open issues or submit pull requests. All contributions are valued!

❤️ Support the Project
Open‑Lotto is a passion‑driven project that I build and maintain in my free time. If you find it useful, enjoy the transparency behind the engine, or want to help accelerate development, your support makes a real difference.

https://github.com/sponsors/Boussetta

Your contribution helps me dedicate more time to improving the system, adding new features, and keeping the project open for everyone.

Thank you for supporting independent open‑source work.