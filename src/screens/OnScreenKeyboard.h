#pragma once
#include <GfxRenderer.h>
#include <InputManager.h>

#include <functional>
#include <string>

/**
 * Reusable on-screen keyboard component for text input.
 * Can be embedded in any screen that needs text entry.
 *
 * Usage:
 *   1. Create an OnScreenKeyboard instance
 *   2. Call render() to draw the keyboard
 *   3. Call handleInput() to process button presses
 *   4. When isComplete() returns true, get the result from getText()
 *   5. Call isCancelled() to check if user cancelled input
 */
class OnScreenKeyboard {
 public:
  // Callback types
  using OnCompleteCallback = std::function<void(const std::string&)>;
  using OnCancelCallback = std::function<void()>;

  /**
   * Constructor
   * @param renderer Reference to the GfxRenderer for drawing
   * @param inputManager Reference to InputManager for handling input
   * @param title Title to display above the keyboard
   * @param initialText Initial text to show in the input field
   * @param maxLength Maximum length of input text (0 for unlimited)
   * @param isPassword If true, display asterisks instead of actual characters
   */
  OnScreenKeyboard(GfxRenderer& renderer, InputManager& inputManager, const std::string& title = "Enter Text",
                   const std::string& initialText = "", size_t maxLength = 0, bool isPassword = false);

  /**
   * Handle button input. Call this in your screen's handleInput().
   * @return true if input was handled, false otherwise
   */
  bool handleInput();

  /**
   * Render the keyboard at the specified Y position.
   * @param startY Y-coordinate where keyboard rendering starts
   */
  void render(int startY) const;

  /**
   * Get the current text entered by the user.
   */
  const std::string& getText() const { return text; }

  /**
   * Set the current text.
   */
  void setText(const std::string& newText);

  /**
   * Check if the user has completed text entry (pressed OK on Done).
   */
  bool isComplete() const { return complete; }

  /**
   * Check if the user has cancelled text entry.
   */
  bool isCancelled() const { return cancelled; }

  /**
   * Reset the keyboard state for reuse.
   */
  void reset(const std::string& newTitle = "", const std::string& newInitialText = "");

  /**
   * Set callback for when input is complete.
   */
  void setOnComplete(OnCompleteCallback callback) { onComplete = callback; }

  /**
   * Set callback for when input is cancelled.
   */
  void setOnCancel(OnCancelCallback callback) { onCancel = callback; }

 private:
  GfxRenderer& renderer;
  InputManager& inputManager;

  std::string title;
  std::string text;
  size_t maxLength;
  bool isPassword;

  // Keyboard state
  int selectedRow = 0;
  int selectedCol = 0;
  bool shiftActive = false;
  bool complete = false;
  bool cancelled = false;

  // Callbacks
  OnCompleteCallback onComplete;
  OnCancelCallback onCancel;

  // Keyboard layout
  static constexpr int NUM_ROWS = 5;
  static constexpr int KEYS_PER_ROW = 13;  // Max keys per row (rows 0 and 1 have 13 keys)
  static const char* const keyboard[NUM_ROWS];
  static const char* const keyboardShift[NUM_ROWS];

  // Special key positions (bottom row)
  static constexpr int SHIFT_ROW = 4;
  static constexpr int SHIFT_COL = 0;
  static constexpr int SPACE_ROW = 4;
  static constexpr int SPACE_COL = 2;
  static constexpr int BACKSPACE_ROW = 4;
  static constexpr int BACKSPACE_COL = 7;
  static constexpr int DONE_ROW = 4;
  static constexpr int DONE_COL = 9;

  char getSelectedChar() const;
  void handleKeyPress();
  int getRowLength(int row) const;
};
