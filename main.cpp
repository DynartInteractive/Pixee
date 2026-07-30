#include <cstdio>
#include <cstring>

#include "Pixee.h"

// Supplied by Pixee.pro from the VERSION file; fallback for non-qmake builds.
#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

int main(int argc, char *argv[])
{
    // Answer --version / -v before spinning up the GUI. On Windows this app is
    // built for the GUI subsystem, so the text is only visible when stdout is
    // redirected (e.g. `Pixee.exe --version > v.txt`); on Linux it prints to
    // the terminal as usual.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            std::printf("Pixee %s\n", APP_VERSION);
            return 0;
        }
    }

    Pixee pixee(argc, argv);
    return pixee.run();
}
