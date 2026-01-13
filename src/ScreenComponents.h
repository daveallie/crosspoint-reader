#pragma once

#include <vector>

class GfxRenderer;

struct TabInfo {
  const char* label;
  bool selected;
};

class ScreenComponents {
 public:
  static void drawBattery(const GfxRenderer& renderer, int left, int top);

  // Draw a horizontal tab bar with underline indicator for selected tab
  // Returns the height of the tab bar (for positioning content below)
  static int drawTabBar(const GfxRenderer& renderer, int y, const std::vector<TabInfo>& tabs);

  // Draw a scroll/page indicator on the right side of the screen
  // Shows up/down arrows and current page fraction (e.g., "1/3")
  static void drawScrollIndicator(const GfxRenderer& renderer, int currentPage, int totalPages, int contentTop,
                                  int contentHeight);
};
