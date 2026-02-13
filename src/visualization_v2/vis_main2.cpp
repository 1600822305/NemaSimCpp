#include "visualization_v2/vis_app2.h"

int main(int /*argc*/, char* /*argv*/[]) {
    celegans::VisApp2 app;
    if (!app.initialize(1920, 1080)) {
        return 1;
    }
    app.run();
    return 0;
}