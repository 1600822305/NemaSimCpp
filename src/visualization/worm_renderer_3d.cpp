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

// ================================================================
// Draw the worm body using ImGui DrawList
// Top-down view with body outline, tapering, and muscle activation colors
// This is driven by the REAL 302-neuron SimulationEngine
// ================================================================
void WormRenderer3D::draw(const std::array<BodySegment, NUM_BODY_SEGMENTS>& segments) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wpos = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float panel_w = avail.x;
    float panel_h = avail.y - 20.0f;  // leave space for text
    if (panel_w < 50 || panel_h < 50) return;

    // Use half for top view, half for side view
    float view_h = panel_h * 0.5f;

    // --- Compute body center and extent ---
    float cx = 0, cy = 0;
    for (int i = 0; i < NUM_BODY_SEGMENTS; i++) {
        cx += (float)segments[i].position.x;
        cy += (float)segments[i].position.y;
    }
    cx /= NUM_BODY_SEGMENTS;
    cy /= NUM_BODY_SEGMENTS;

    // Find extent for auto-scaling
    float max_extent = 0.05f;
    for (int i = 0; i < NUM_BODY_SEGMENTS; i++) {
        float dx = (float)segments[i].position.x - cx;
        float dy = (float)segments[i].position.y - cy;
        float ext = sqrtf(dx * dx + dy * dy);
        if (ext > max_extent) max_extent = ext;
    }

    float scale = (std::min(panel_w, view_h) * 0.4f) / (max_extent + 0.05f);
    float body_radius_mm = 0.04f;

    // ============================
    // TOP VIEW (XY plane)
    // ============================
    {
        float ox = wpos.x + panel_w * 0.5f;
        float oy = wpos.y + view_h * 0.5f;

        // Background
        dl->AddRectFilled(ImVec2(wpos.x, wpos.y), ImVec2(wpos.x + panel_w, wpos.y + view_h),
                          IM_COL32(12, 12, 20, 255));
        dl->AddText(ImVec2(wpos.x + 4, wpos.y + 2), IM_COL32(100, 180, 255, 200), u8"TOP (XY)");

        // Grid lines
        for (int g = -4; g <= 4; g++) {
            float gx = ox + g * scale * 0.1f;
            float gy = oy + g * scale * 0.1f;
            dl->AddLine(ImVec2(gx, wpos.y), ImVec2(gx, wpos.y + view_h), IM_COL32(30, 30, 50, 100));
            dl->AddLine(ImVec2(wpos.x, gy), ImVec2(wpos.x + panel_w, gy), IM_COL32(30, 30, 50, 100));
        }

        // Draw body outline as filled quads per segment
        for (int i = 0; i < NUM_BODY_SEGMENTS - 1; i++) {
            float x0 = ox + ((float)segments[i].position.x - cx) * scale;
            float y0 = oy - ((float)segments[i].position.y - cy) * scale;
            float x1 = ox + ((float)segments[i+1].position.x - cx) * scale;
            float y1 = oy - ((float)segments[i+1].position.y - cy) * scale;

            float a0 = (float)segments[i].angle;
            float a1 = (float)segments[i+1].angle;

            // Prolate ellipsoid tapering
            float s0 = (float)i / (NUM_BODY_SEGMENTS - 1);
            float s1 = (float)(i + 1) / (NUM_BODY_SEGMENTS - 1);
            float t0 = sqrtf(1.0f - (2.0f * s0 - 1.0f) * (2.0f * s0 - 1.0f) * 0.95f);
            float t1 = sqrtf(1.0f - (2.0f * s1 - 1.0f) * (2.0f * s1 - 1.0f) * 0.95f);
            float r0 = body_radius_mm * t0 * scale;
            float r1 = body_radius_mm * t1 * scale;

            // Perpendicular offset (normal to body axis)
            float nx0 = -sinf(a0), ny0 = cosf(a0);
            float nx1 = -sinf(a1), ny1 = cosf(a1);

            // Quad corners (dorsal side)
            ImVec2 p0d(x0 + nx0 * r0, y0 - ny0 * r0);
            ImVec2 p1d(x1 + nx1 * r1, y1 - ny1 * r1);
            // Ventral side
            ImVec2 p0v(x0 - nx0 * r0, y0 + ny0 * r0);
            ImVec2 p1v(x1 - nx1 * r1, y1 + ny1 * r1);

            // Muscle activation colors
            float d_act = (float)segments[i].dorsal_activation;
            float v_act = (float)segments[i].ventral_activation;

            // Dorsal half: orange/red when active
            ImU32 col_d;
            if (d_act > 0.05f) {
                int r = (int)(100 + d_act * 155);
                int g = (int)(60 + d_act * 50);
                int b = 30;
                col_d = IM_COL32(r, g, b, 220);
            } else {
                col_d = IM_COL32(60, 75, 65, 200);
            }

            // Ventral half: blue/cyan when active
            ImU32 col_v;
            if (v_act > 0.05f) {
                int r = 30;
                int g = (int)(70 + v_act * 80);
                int b = (int)(100 + v_act * 155);
                col_v = IM_COL32(r, g, b, 220);
            } else {
                col_v = IM_COL32(55, 65, 75, 200);
            }

            // Draw dorsal quad
            dl->AddQuadFilled(p0d, p1d, ImVec2((x1 + x0) * 0.5f, (y1 + y0) * 0.5f),
                              ImVec2((x0 + x0) * 0.5f, (y0 + y0) * 0.5f), col_d);
            // Draw as two triangles for cleaner fill
            ImVec2 mid0(x0, y0);
            ImVec2 mid1(x1, y1);
            dl->AddTriangleFilled(p0d, p1d, mid1, col_d);
            dl->AddTriangleFilled(p0d, mid1, mid0, col_d);
            dl->AddTriangleFilled(p0v, p1v, mid1, col_v);
            dl->AddTriangleFilled(p0v, mid1, mid0, col_v);

            // Outline
            dl->AddLine(p0d, p1d, IM_COL32(120, 160, 120, 180), 1.0f);
            dl->AddLine(p0v, p1v, IM_COL32(100, 120, 160, 180), 1.0f);
        }

        // Head marker (green circle)
        float hx = ox + ((float)segments[0].position.x - cx) * scale;
        float hy = oy - ((float)segments[0].position.y - cy) * scale;
        dl->AddCircleFilled(ImVec2(hx, hy), 4.0f, IM_COL32(50, 255, 50, 255));
        dl->AddText(ImVec2(hx + 6, hy - 8), IM_COL32(50, 255, 50, 200), "H");

        // Tail marker (blue)
        float tx_pos = ox + ((float)segments[NUM_BODY_SEGMENTS-1].position.x - cx) * scale;
        float ty_pos = oy - ((float)segments[NUM_BODY_SEGMENTS-1].position.y - cy) * scale;
        dl->AddCircleFilled(ImVec2(tx_pos, ty_pos), 3.0f, IM_COL32(100, 100, 255, 255));
    }

    // ============================
    // SIDE VIEW (curvature kymograph)
    // ============================
    {
        float side_top = wpos.y + view_h + 2;
        float side_h = view_h - 4;
        float ox = wpos.x + 30;
        float bar_w = (panel_w - 60) / NUM_BODY_SEGMENTS;

        // Background
        dl->AddRectFilled(ImVec2(wpos.x, side_top), ImVec2(wpos.x + panel_w, side_top + side_h),
                          IM_COL32(12, 12, 20, 255));
        dl->AddText(ImVec2(wpos.x + 4, side_top + 2), IM_COL32(255, 200, 100, 200), u8"CURVATURE");
        dl->AddText(ImVec2(wpos.x + 4, side_top + 14), IM_COL32(150, 150, 150, 150), u8"H          T");

        // Zero line
        float zero_y = side_top + side_h * 0.5f;
        dl->AddLine(ImVec2(ox, zero_y), ImVec2(ox + bar_w * NUM_BODY_SEGMENTS, zero_y),
                    IM_COL32(80, 80, 80, 150), 1.0f);

        // Draw curvature bars
        float curv_scale = side_h * 0.15f;  // pixels per 1/mm curvature
        for (int i = 0; i < NUM_BODY_SEGMENTS; i++) {
            float curv = (float)segments[i].curvature;
            float bar_height = curv * curv_scale;
            float bx = ox + i * bar_w;

            ImU32 col;
            if (curv > 0) {
                // Dorsal bend: warm
                int r = (int)(120 + std::min(1.0f, std::abs(curv) / 3.0f) * 135);
                int g = (int)(80 + std::min(1.0f, std::abs(curv) / 3.0f) * 60);
                col = IM_COL32(r, g, 30, 200);
            } else {
                // Ventral bend: cool
                int b = (int)(120 + std::min(1.0f, std::abs(curv) / 3.0f) * 135);
                int g = (int)(80 + std::min(1.0f, std::abs(curv) / 3.0f) * 60);
                col = IM_COL32(30, g, b, 200);
            }

            if (bar_height > 0) {
                dl->AddRectFilled(ImVec2(bx, zero_y - bar_height), ImVec2(bx + bar_w - 1, zero_y), col);
            } else {
                dl->AddRectFilled(ImVec2(bx, zero_y), ImVec2(bx + bar_w - 1, zero_y - bar_height), col);
            }
        }

        // Labels
        dl->AddText(ImVec2(ox + bar_w * NUM_BODY_SEGMENTS + 4, zero_y - side_h * 0.3f),
                    IM_COL32(255, 180, 50, 180), u8"D");
        dl->AddText(ImVec2(ox + bar_w * NUM_BODY_SEGMENTS + 4, zero_y + side_h * 0.15f),
                    IM_COL32(50, 150, 255, 180), u8"V");
    }

    // Advance cursor past drawn area
    ImGui::Dummy(ImVec2(panel_w, panel_h));
}

} // namespace celegans
