#pragma once

namespace ssf2t {

// The in-game corner readouts: one-line panels stacked down from the top-right corner with their right edges aligned.
// Every size follows the font size, so the layout scales with DPI.
struct CornerPanel {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  float text_x = 0, text_y = 0;
};

inline CornerPanel CornerPanelAt(float display_width, float font_size, float text_width, float text_height, int slot) {
  const float pad_x = font_size * 0.6f;
  const float pad_y = font_size * 0.3f;
  const float height = text_height + pad_y * 2;
  const float gap = font_size * 0.25f;
  CornerPanel panel;
  panel.x1 = display_width - font_size;
  panel.x0 = panel.x1 - text_width - pad_x * 2;
  panel.y0 = font_size + float(slot) * (height + gap);
  panel.y1 = panel.y0 + height;
  panel.text_x = panel.x0 + pad_x;
  panel.text_y = panel.y0 + pad_y;
  return panel;
}

// The frame counter takes the first slot; Ping goes under it when both are shown.
constexpr int FrameCounterSlot() { return 0; }
constexpr int PingSlot(bool frame_counter_shown) { return frame_counter_shown ? 1 : 0; }

}  // namespace ssf2t
