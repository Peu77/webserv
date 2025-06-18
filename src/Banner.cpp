#include "Banner.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <cstdlib>

using namespace std;
using namespace chrono;

const vector<string> colors = {
    "\033[31m", // Red
    "\033[32m", // Green
    "\033[33m", // Yellow
    "\033[34m", // Blue
    "\033[35m", // Magenta
    "\033[36m"  // Cyan
};

const string reset = "\033[0m";

static const vector<string> banner = {
    "__        __   _                __  __ ",
    "\\ \\      / /__| |__  ___ _   _ / _|/ _|",
    " \\ \\ /\\ / / _ \\ '_ \\/ __| | | | |_| |_ ",
    "  \\ V  V /  __/ |_) \\__ \\ |_| |  _|  _|",
    "   \\_/\\_/ \\___|_.__/|___/\\__,_|_| |_|  "
};
static void clearScreen() {
    cout << "\033[2J\033[H";
}

void Banner::printBannerAnimation() {
    int bannerWidth = banner[0].length();
    constexpr int finalPosition = 0;


    for (int offset = -bannerWidth; offset <= finalPosition; ++offset) {
        clearScreen();

        for (size_t i = 0; i < banner.size(); ++i) {
            const string color = colors[i % colors.size()];
            cout << color;

            if (offset < 0) {
                cout << banner[i].substr(-offset);
            } else {
                cout << string(offset, ' ') << banner[i];
            }

            cout << reset << endl;
        }

        this_thread::sleep_for(milliseconds(10));
    }

    cout << flush;
}