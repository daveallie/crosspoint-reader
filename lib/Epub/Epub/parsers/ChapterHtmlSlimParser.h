#pragma once

#include <expat.h>

#include <climits>
#include <cstring>
#include <functional>
#include <memory>

#include "../FootnoteEntry.h"
#include "../ParsedText.h"
#include "../blocks/TextBlock.h"

class Page;
class GfxRenderer;

#define MAX_WORD_SIZE 200

struct Noteref {
  char number[16];
  char href[128];
};

// Struct to store collected inline footnotes (aside elements)
struct InlineFootnote {
  char id[3];
  char* text;

  InlineFootnote() : text(nullptr) { id[0] = '\0'; }
};

// Struct to store collected inline footnotes from <p class="note">
struct ParagraphNote {
  char id[16];  // ID from <a id="rnote1">
  char* text;   // Pointer to dynamically allocated text

  ParagraphNote() : text(nullptr) { id[0] = '\0'; }

  ~ParagraphNote() {
    if (text) {
      free(text);
      text = nullptr;
    }
  }

  ParagraphNote(const ParagraphNote&) = delete;
  ParagraphNote& operator=(const ParagraphNote&) = delete;
};

class ChapterHtmlSlimParser {
  const char* filepath;
  GfxRenderer& renderer;
  std::function<void(std::unique_ptr<Page>)> completePageFn;
  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;
  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  int fontId;
  float lineCompression;
  int marginTop;
  int marginRight;
  int marginBottom;
  int marginLeft;
  bool extraParagraphSpacing;

  // Noteref tracking
  bool insideNoteref = false;
  char currentNoterefText[16] = {0};
  int currentNoterefTextLen = 0;
  char currentNoterefHref[128] = {0};
  int currentNoterefHrefLen = 0;
  std::function<void(Noteref&)> noterefCallback = nullptr;

  // Footnote tracking for current page
  FootnoteEntry currentPageFootnotes[16];
  int currentPageFootnoteCount = 0;

  // Inline footnotes (aside) tracking
  bool insideAsideFootnote = false;
  int asideDepth = 0;
  char currentAsideId[3] = {0};

  // Paragraph note tracking
  bool insideParagraphNote = false;
  int paragraphNoteDepth = 0;
  char currentParagraphNoteId[16] = {0};
  static constexpr int MAX_PNOTE_BUFFER = 512;
  char currentParagraphNoteText[MAX_PNOTE_BUFFER] = {0};
  int currentParagraphNoteTextLen = 0;

  // Temporary buffer for accumulation, will be copied to dynamic allocation
  static constexpr int MAX_ASIDE_BUFFER = 2048;
  char currentAsideText[MAX_ASIDE_BUFFER] = {0};
  int currentAsideTextLen = 0;

  // Flag to indicate we're in Pass 1 (collecting asides only)
  bool isPass1CollectingAsides = false;

  // Cache dir path for generating HTML files
  std::string cacheDir;

  void addFootnoteToCurrentPage(const char* number, const char* href);
  void startNewTextBlock(TextBlock::BLOCK_STYLE style);
  void makePages();

  // XML callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  static void XMLCALL endElement(void* userData, const XML_Char* name);

 public:
  // inline footnotes
  InlineFootnote inlineFootnotes[16];
  int inlineFootnoteCount = 0;
  // paragraph notes
  ParagraphNote paragraphNotes[32];
  int paragraphNoteCount = 0;

  explicit ChapterHtmlSlimParser(const char* filepath, GfxRenderer& renderer, const int fontId,
                                 const float lineCompression, const int marginTop, const int marginRight,
                                 const int marginBottom, const int marginLeft, const bool extraParagraphSpacing,
                                 const std::function<void(std::unique_ptr<Page>)>& completePageFn,
                                 const std::string& cacheDir = "")
      : filepath(filepath),
        renderer(renderer),
        completePageFn(completePageFn),
        fontId(fontId),
        lineCompression(lineCompression),
        marginTop(marginTop),
        marginRight(marginRight),
        marginBottom(marginBottom),
        marginLeft(marginLeft),
        extraParagraphSpacing(extraParagraphSpacing),
        cacheDir(cacheDir),
        inlineFootnoteCount(0) {
    // Initialize all footnote pointers to null
    for (int i = 0; i < 16; i++) {
      inlineFootnotes[i].text = nullptr;
      inlineFootnotes[i].id[0] = '\0';
    }
  }

  ~ChapterHtmlSlimParser() {
    // Manual cleanup of inline footnotes
    for (int i = 0; i < inlineFootnoteCount; i++) {
      if (inlineFootnotes[i].text) {
        free(inlineFootnotes[i].text);
        inlineFootnotes[i].text = nullptr;
      }
    }
  }

  bool parseAndBuildPages();
  void addLineToPage(std::shared_ptr<TextBlock> line);

  void setNoterefCallback(const std::function<void(Noteref&)>& callback) { noterefCallback = callback; }
};