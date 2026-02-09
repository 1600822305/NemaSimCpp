#include "visualization/vis_app.h"

int main(int /*argc*/, char* /*argv*/[]) {
    celegans::VisApp app;
    if (!app.initialize(1400, 900)) {
        return 1;
    }
    app.run();
    return 0;
}
