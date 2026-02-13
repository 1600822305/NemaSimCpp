#include "visualization/worm_renderer_3d.h"
#include "core/logger.h"

#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace celegans {

bool WormRenderer3D::initialize(int width, int height) {
    width_ = width;
    height_ = height;
    LOG_INFO("WormRenderer3D (DrawList) initialized: ", width, "x", height);
    return true;
}

// Helper: blend two ImU32 colors
static ImU32 blend_color(ImU32 a, ImU32 b, float t) {
    int ra = (a >> 0) & 0xFF, ga = (a >> 8) & 0xFF, ba = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
    int rb = (b >> 0) & 0xFF, gb = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF, ab = (b >> 24) & 0xFF;
    int ro = (int)(ra + (rb - ra) * t);
    int go = (int)(ga + (gb - ga) * t);
    int bo = (int)(ba + (bb - ba) * t);
    int ao = (int)(aa + (ab - aa) * t);
    return IM_COL32(ro, go, bo, ao);
}

// Helper: muscle activation to color (Sibernetic-style: red=active, beige=relaxed)
static ImU32 muscle_color(float activation, float light) {
    // Relaxed: translucent beige skin
    // Active: bright red/orange (like Sibernetic)
    float a = std::clamp(activation, 0.0f, 1.0f);
    float l = std::clamp(light, 0.3f, 1.0f);  // lighting factor

    int r = (int)((80 + a * 175) * l);
    int g = (int)((90 + a * 20 - a * a * 60) * l);
    int b = (int)((75 - a * 55) * l);
    return IM_COL32(std::min(r, 255), std::min(g, 255), std::max(b, 0), 230);
}

// ================================================================
// Draw the worm body — smooth tubular shape with pseudo-3D shading
// Inspired by OpenWorm Sibernetic's rendering style
// Driven by REAL 302-neuron SimulationEngine
// ================================================================
void WormRenderer3D::draw(const std::array<BodySegment, NUM_BODY_SEGMENTS>& segments) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wpos = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float panel_w = avail.x;
    float panel_h = avail.y - 18.0f;
    if (panel_w < 50 || panel_h < 50) return;

    float view_h = panel_h * 0.55f;
    float curv_h = panel_h - view_h - 4;

    // --- Compute body center and extent ---
    float cx = 0, cy = 0;
    for (int i = 0; i < NUM_BODY_SEGMENTS; i++) {
        cx += (float)segments[i].position.x;
        cy += (float)segments[i].position.y;
    }
    cx /= NUM_BODY_SEGMENTS;
    cy /= NUM_BODY_SEGMENTS;

    float max_extent = 0.05f;
    for (int i = 0; i < NUM_BODY_SEGMENTS; i++) {
        float dx = (float)segments[i].position.x - cx;
        float dy = (float)segments[i].position.y - cy;
        float ext = sqrtf(dx * dx + dy * dy);
        if (ext > max_extent) max_extent = ext;
    }

    float scale = (std::min(panel_w, view_h) * 0.38f) / (max_extent + 0.05f);
    float body_radius_mm = 0.04f;

    // ========================================
    // TOP VIEW — smooth tubular body with 3D shading
    // ========================================
    {
        float ox = wpos.x + panel_w * 0.5f;
        float oy = wpos.y + view_h * 0.5f;

        // Dark background with subtle gradient
        dl->AddRectFilledMultiColor(
            ImVec2(wpos.x, wpos.y), ImVec2(wpos.x + panel_w, wpos.y + view_h),
            IM_COL32(8, 12, 18, 255), IM_COL32(12, 12, 22, 255),
            IM_COL32(10, 10, 20, 255), IM_COL32(8, 14, 16, 255));

        // Subtle grid
        for (int g = -6; g <= 6; g++) {
            float gx = ox + g * scale * 0.08f;
            float gy = oy + g * scale * 0.08f;
            if (gx > wpos.x && gx < wpos.x + panel_w)
                dl->AddLine(ImVec2(gx, wpos.y), ImVec2(gx, wpos.y + view_h), IM_COL32(25, 30, 40, 60));
            if (gy > wpos.y && gy < wpos.y + view_h)
                dl->AddLine(ImVec2(wpos.x, gy), ImVec2(wpos.x + panel_w, gy), IM_COL32(25, 30, 40, 60));
        }

        // Pre-compute screen positions and radii
        struct NodeInfo { float x, y, nx, ny, r; float d_act, v_act; };
        NodeInfo nodes[NUM_BODY_SEGMENTS];
        for (int i = 0; i < NUM_BODY_SEGMENTS; i++) {
            float s = (float)i / (NUM_BODY_SEGMENTS - 1);
            float taper = sqrtf(1.0f - (2.0f * s - 1.0f) * (2.0f * s - 1.0f) * 0.96f);
            nodes[i].x = ox + ((float)segments[i].position.x - cx) * scale;
            nodes[i].y = oy - ((float)segments[i].position.y - cy) * scale;
            float a = (float)segments[i].angle;
            nodes[i].nx = -sinf(a);
            nodes[i].ny = cosf(a);
            nodes[i].r = body_radius_mm * taper * scale;
            nodes[i].d_act = (float)segments[i].dorsal_activation;
            nodes[i].v_act = (float)segments[i].ventral_activation;
        }

        // === Draw body as layered strips for pseudo-3D effect ===
        // 3 layers: shadow (bottom), body fill, highlight (top)
        constexpr int STRIPS = 5;  // number of strips across body width

        for (int strip = 0; strip < STRIPS; strip++) {
            float t0 = (float)strip / STRIPS;
            float t1 = (float)(strip + 1) / STRIPS;
            // Map strip to [-1, 1] across body width
            float w0 = -1.0f + 2.0f * t0;
            float w1 = -1.0f + 2.0f * t1;

            // Pseudo-3D lighting: center strip is brightest
            float mid_w = (w0 + w1) * 0.5f;
            float light = 1.0f - 0.5f * mid_w * mid_w;  // parabolic falloff

            for (int i = 0; i < NUM_BODY_SEGMENTS - 1; i++) {
                auto& n0 = nodes[i];
                auto& n1 = nodes[i + 1];

                // Strip edge positions
                ImVec2 p00(n0.x + n0.nx * n0.r * w0, n0.y - n0.ny * n0.r * w0);
                ImVec2 p01(n0.x + n0.nx * n0.r * w1, n0.y - n0.ny * n0.r * w1);
                ImVec2 p10(n1.x + n1.nx * n1.r * w0, n1.y - n1.ny * n1.r * w0);
                ImVec2 p11(n1.x + n1.nx * n1.r * w1, n1.y - n1.ny * n1.r * w1);

                // Muscle activation: blend dorsal/ventral based on strip position
                float dorsal_w = std::max(0.0f, -mid_w);  // dorsal = negative w
                float ventral_w = std::max(0.0f, mid_w);   // ventral = positive w
                float act = n0.d_act * dorsal_w + n0.v_act * ventral_w +
                           (n0.d_act + n0.v_act) * 0.3f * (1.0f - dorsal_w - ventral_w);

                ImU32 col = muscle_color(act, light);
                dl->AddQuadFilled(p00, p10, p11, p01, col);
            }
        }

        // Smooth outline (dorsal + ventral edges)
        for (int i = 0; i < NUM_BODY_SEGMENTS - 1; i++) {
            auto& n0 = nodes[i];
            auto& n1 = nodes[i + 1];
            ImVec2 d0(n0.x + n0.nx * n0.r, n0.y - n0.ny * n0.r);
            ImVec2 d1(n1.x + n1.nx * n1.r, n1.y - n1.ny * n1.r);
            ImVec2 v0(n0.x - n0.nx * n0.r, n0.y + n0.ny * n0.r);
            ImVec2 v1(n1.x - n1.nx * n1.r, n1.y + n1.ny * n1.r);
            dl->AddLine(d0, d1, IM_COL32(140, 110, 80, 120), 1.2f);
            dl->AddLine(v0, v1, IM_COL32(140, 110, 80, 120), 1.2f);
        }

        // Specular highlight along centerline
        for (int i = 0; i < NUM_BODY_SEGMENTS - 1; i++) {
            auto& n0 = nodes[i];
            auto& n1 = nodes[i + 1];
            // Offset slightly toward "light source" (upper-left)
            float off = n0.r * 0.2f;
            ImVec2 h0(n0.x + n0.nx * off, n0.y - n0.ny * off);
            ImVec2 h1(n1.x + n1.nx * off, n1.y - n1.ny * off);
            float act = std::max(n0.d_act, n0.v_act);
            int alpha = (int)(40 + act * 60);
            dl->AddLine(h0, h1, IM_COL32(255, 240, 200, alpha), 1.5f);
        }

        // Head: green glow
        dl->AddCircleFilled(ImVec2(nodes[0].x, nodes[0].y), nodes[0].r * 1.3f,
                           IM_COL32(20, 80, 20, 100));
        dl->AddCircleFilled(ImVec2(nodes[0].x, nodes[0].y), nodes[0].r * 0.8f,
                           IM_COL32(50, 200, 50, 200));
        dl->AddText(ImVec2(nodes[0].x + nodes[0].r + 3, nodes[0].y - 6),
                   IM_COL32(80, 255, 80, 220), "HEAD");

        // Tail: subtle blue
        int t = NUM_BODY_SEGMENTS - 1;
        dl->AddCircleFilled(ImVec2(nodes[t].x, nodes[t].y), nodes[t].r * 0.6f,
                           IM_COL32(80, 80, 200, 180));

        // Title
        dl->AddText(ImVec2(wpos.x + 4, wpos.y + 2),
                   IM_COL32(100, 200, 255, 220), u8"C. elegans \u4fef\u89c6\u56fe (302\u795e\u7ecf\u5143\u5b9e\u65f6\u9a71\u52a8)");
    }

    // ========================================
    // CURVATURE WAVE display
    // ========================================
    {
        float side_top = wpos.y + view_h + 4;
        float ox = wpos.x + 30;
        float bar_w = (panel_w - 50) / NUM_BODY_SEGMENTS;

        // Background
        dl->AddRectFilled(ImVec2(wpos.x, side_top), ImVec2(wpos.x + panel_w, side_top + curv_h),
                          IM_COL32(10, 10, 16, 255));

        float zero_y = side_top + curv_h * 0.5f;
        dl->AddLine(ImVec2(ox, zero_y), ImVec2(ox + bar_w * NUM_BODY_SEGMENTS, zero_y),
                    IM_COL32(60, 60, 70, 150), 1.0f);

        // Draw curvature as smooth filled area (not bars)
        float curv_scale = curv_h * 0.12f;
        ImVec2 top_pts[NUM_BODY_SEGMENTS + 2];
        ImVec2 bot_pts[NUM_BODY_SEGMENTS + 2];
        int n_top = 0, n_bot = 0;

        for (int i = 0; i < NUM_BODY_SEGMENTS; i++) {
            float curv = (float)segments[i].curvature;
            float bx = ox + (i + 0.5f) * bar_w;
            float bar_height = curv * curv_scale;

            // Filled area above/below zero
            if (curv > 0.01f) {
                ImU32 col = IM_COL32(
                    (int)(150 + std::min(1.0f, curv / 3.0f) * 105),
                    (int)(60 + std::min(1.0f, curv / 3.0f) * 40),
                    20, 180);
                dl->AddRectFilled(ImVec2(bx - bar_w * 0.4f, zero_y - bar_height),
                                 ImVec2(bx + bar_w * 0.4f, zero_y), col);
            } else if (curv < -0.01f) {
                ImU32 col = IM_COL32(
                    20,
                    (int)(60 + std::min(1.0f, -curv / 3.0f) * 60),
                    (int)(150 + std::min(1.0f, -curv / 3.0f) * 105), 180);
                dl->AddRectFilled(ImVec2(bx - bar_w * 0.4f, zero_y),
                                 ImVec2(bx + bar_w * 0.4f, zero_y - bar_height), col);
            }
        }

        // Curvature line on top
        for (int i = 0; i < NUM_BODY_SEGMENTS - 1; i++) {
            float c0 = (float)segments[i].curvature;
            float c1 = (float)segments[i + 1].curvature;
            float x0 = ox + (i + 0.5f) * bar_w;
            float x1 = ox + (i + 1.5f) * bar_w;
            dl->AddLine(ImVec2(x0, zero_y - c0 * curv_scale),
                       ImVec2(x1, zero_y - c1 * curv_scale),
                       IM_COL32(255, 220, 100, 220), 2.0f);
        }

        // Labels
        dl->AddText(ImVec2(wpos.x + 4, side_top + 2), IM_COL32(255, 220, 100, 200), u8"\u66f2\u7387\u6ce2");
        dl->AddText(ImVec2(ox - 20, side_top + 2), IM_COL32(100, 200, 100, 180), u8"H");
        dl->AddText(ImVec2(ox + bar_w * NUM_BODY_SEGMENTS + 4, side_top + 2),
                   IM_COL32(100, 100, 200, 180), u8"T");
        dl->AddText(ImVec2(wpos.x + panel_w - 30, zero_y - curv_h * 0.25f),
                   IM_COL32(255, 150, 50, 160), u8"D");
        dl->AddText(ImVec2(wpos.x + panel_w - 30, zero_y + curv_h * 0.1f),
                   IM_COL32(50, 150, 255, 160), u8"V");
    }

    ImGui::Dummy(ImVec2(panel_w, panel_h));
}

} // namespace celegans
