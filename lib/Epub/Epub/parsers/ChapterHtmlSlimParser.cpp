#include "ChapterHtmlSlimParser.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SD.h>
#include <expat.h>

#include "../Page.h"
#include "../htmlEntities.h"

const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr int NUM_HEADER_TAGS = sizeof(HEADER_TAGS) / sizeof(HEADER_TAGS[0]);

const char* BLOCK_TAGS[] = {"p", "li", "div", "br"};
constexpr int NUM_BLOCK_TAGS = sizeof(BLOCK_TAGS) / sizeof(BLOCK_TAGS[0]);

const char* BOLD_TAGS[] = {"b"};
constexpr int NUM_BOLD_TAGS = sizeof(BOLD_TAGS) / sizeof(BOLD_TAGS[0]);

const char* ITALIC_TAGS[] = {"i"};
constexpr int NUM_ITALIC_TAGS = sizeof(ITALIC_TAGS) / sizeof(ITALIC_TAGS[0]);

const char* IMAGE_TAGS[] = {"img"};
constexpr int NUM_IMAGE_TAGS = sizeof(IMAGE_TAGS) / sizeof(IMAGE_TAGS[0]);

const char* SKIP_TAGS[] = {"head", "table"};
constexpr int NUM_SKIP_TAGS = sizeof(SKIP_TAGS) / sizeof(SKIP_TAGS[0]);

bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

bool matches(const char* tag_name, const char* possible_tags[], const int possible_tag_count) {
  for (int i = 0; i < possible_tag_count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

const char* getAttribute(const XML_Char** atts, const char* attrName) {
  if (!atts) return nullptr;

  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], attrName) == 0) {
      return atts[i + 1];
    }
  }
  return nullptr;
}

void ChapterHtmlSlimParser::startNewTextBlock(const TextBlock::BLOCK_STYLE style) {
  if (currentTextBlock) {
    if (currentTextBlock->isEmpty()) {
      currentTextBlock->setStyle(style);
      return;
    }
    makePages();
  }
  currentTextBlock.reset(new ParsedText(style, extraParagraphSpacing));
}

void ChapterHtmlSlimParser::addFootnoteToCurrentPage(const char* number, const char* href) {
  if (currentPageFootnoteCount >= 16) return;

  Serial.printf("[%lu] [ADDFT] Adding footnote: num=%s, href=%s\n", millis(), number, href);

  // Copy number
  strncpy(currentPageFootnotes[currentPageFootnoteCount].number, number, 2);
  currentPageFootnotes[currentPageFootnoteCount].number[2] = '\0';

  // Check if this is an inline footnote reference
  const char* hashPos = strchr(href, '#');
  if (hashPos) {
    const char* inlineId = hashPos + 1;  // Skip the '#'

    // Check if we have this inline footnote
    bool foundInline = false;
    for (int i = 0; i < inlineFootnoteCount; i++) {
      if (strcmp(inlineFootnotes[i].id, inlineId) == 0) {
        // This is an inline footnote! Rewrite the href
        char rewrittenHref[64];
        snprintf(rewrittenHref, sizeof(rewrittenHref), "inline_%s.html#%s", inlineId, inlineId);

        strncpy(currentPageFootnotes[currentPageFootnoteCount].href, rewrittenHref, 63);
        currentPageFootnotes[currentPageFootnoteCount].href[63] = '\0';

        Serial.printf("[%lu] [ADDFT] Rewrote inline href to: %s\n", millis(), rewrittenHref);
        foundInline = true;
        break;
      }
    }

    // Check if we have this as a paragraph note
    if (!foundInline) {
      for (int i = 0; i < paragraphNoteCount; i++) {
        if (strcmp(paragraphNotes[i].id, inlineId) == 0) {
          char rewrittenHref[64];
          snprintf(rewrittenHref, sizeof(rewrittenHref), "pnote_%s.html#%s", inlineId, inlineId);

          strncpy(currentPageFootnotes[currentPageFootnoteCount].href, rewrittenHref, 63);
          currentPageFootnotes[currentPageFootnoteCount].href[63] = '\0';

          Serial.printf("[%lu] [ADDFT] Rewrote paragraph note href to: %s\n", millis(), rewrittenHref);
          foundInline = true;
          break;
        }
      }
    }

    if (!foundInline) {
      // Normal href, just copy it
      strncpy(currentPageFootnotes[currentPageFootnoteCount].href, href, 63);
      currentPageFootnotes[currentPageFootnoteCount].href[63] = '\0';
    }
  } else {
    // No anchor, just copy
    strncpy(currentPageFootnotes[currentPageFootnoteCount].href, href, 63);
    currentPageFootnotes[currentPageFootnoteCount].href[63] = '\0';
  }

  currentPageFootnoteCount++;

  Serial.printf("[%lu] [ADDFT] Stored as: num=%s, href=%s\n", millis(),
                currentPageFootnotes[currentPageFootnoteCount - 1].number,
                currentPageFootnotes[currentPageFootnoteCount - 1].href);
}

void XMLCALL ChapterHtmlSlimParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // ============================================================================
  // PASS 1: Detect and collect <p class="note">
  // ============================================================================
  if (strcmp(name, "p") == 0 && self->isPass1CollectingAsides) {
    const char* classAttr = getAttribute(atts, "class");

    if (classAttr && (strcmp(classAttr, "note") == 0 || strstr(classAttr, "note"))) {
      Serial.printf("[%lu] [PNOTE] Found paragraph note (pass1=1)\n", millis());

      self->insideParagraphNote = true;
      self->paragraphNoteDepth = self->depth;
      self->currentParagraphNoteTextLen = 0;
      self->currentParagraphNoteText[0] = '\0';
      self->currentParagraphNoteId[0] = '\0';

      self->depth += 1;
      return;
    }
  }

  // Inside paragraph note in Pass 1, look for <a id="rnoteX">
  if (self->insideParagraphNote && self->isPass1CollectingAsides && strcmp(name, "a") == 0) {
    const char* id = getAttribute(atts, "id");

    if (id && strncmp(id, "rnote", 5) == 0) {
      strncpy(self->currentParagraphNoteId, id, 15);
      self->currentParagraphNoteId[15] = '\0';
      Serial.printf("[%lu] [PNOTE] Found note ID: %s\n", millis(), id);
    }

    self->depth += 1;
    return;
  }

  // ============================================================================
  // PASS 1: Detect and collect <aside epub:type="footnote">
  // ============================================================================
  if (strcmp(name, "aside") == 0) {
    const char* epubType = getAttribute(atts, "epub:type");
    const char* id = getAttribute(atts, "id");

    if (epubType && strcmp(epubType, "footnote") == 0 && id) {
      if (self->isPass1CollectingAsides) {
        // Pass 1: Collect aside
        Serial.printf("[%lu] [ASIDE] Found inline footnote: id=%s (pass1=%d)\n", millis(), id,
                      self->isPass1CollectingAsides);

        self->insideAsideFootnote = true;
        self->asideDepth = self->depth;
        self->currentAsideTextLen = 0;
        self->currentAsideText[0] = '\0';

        strncpy(self->currentAsideId, id, 2);
        self->currentAsideId[2] = '\0';
      } else {
        // Pass 2: Skip the aside (we already have it from Pass 1)
        Serial.printf("[%lu] [ASIDE] Skipping aside in Pass 2: id=%s\n", millis(), id);

        // Find the inline footnote text
        for (int i = 0; i < self->inlineFootnoteCount; i++) {
          if (strcmp(self->inlineFootnotes[i].id, id) == 0 && self->inlineFootnotes[i].text) {
            // Output the footnote text as normal text
            const char* text = self->inlineFootnotes[i].text;
            int textLen = strlen(text);

            // Process it through characterData
            self->characterData(self, text, textLen);

            Serial.printf("[%lu] [ASIDE] Rendered aside text: %.80s...\n", millis(), text);
            break;
          }
        }

        // Skip the aside element itself
        self->skipUntilDepth = self->depth;
      }

      self->depth += 1;
      return;
    }
  }

  // ============================================================================
  // PASS 1: Skip everything else
  // ============================================================================
  if (self->isPass1CollectingAsides) {
    self->depth += 1;
    return;
  }

  // ============================================================================
  // PASS 2: Skip <p class="note"> (we already have them from Pass 1)
  // ============================================================================
  if (strcmp(name, "p") == 0) {
    const char* classAttr = getAttribute(atts, "class");

    if (classAttr && (strcmp(classAttr, "note") == 0 || strstr(classAttr, "note"))) {
      Serial.printf("[%lu] [PNOTE] Skipping paragraph note in Pass 2\n", millis());
      self->skipUntilDepth = self->depth;
      self->depth += 1;
      return;
    }
  }

  // ============================================================================
  // PASS 2: Normal parsing
  // ============================================================================

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  // Rest of startElement logic for pass 2...
  if (strcmp(name, "a") == 0) {
    const char* epubType = getAttribute(atts, "epub:type");
    const char* href = getAttribute(atts, "href");

    // Detect epub:type="noteref" OR href="#rnoteX" pattern
    bool isNoteref = (epubType && strcmp(epubType, "noteref") == 0);

    // Also detect links with href starting with "#rnote" (reverse note pattern)
    if (!isNoteref && href && href[0] == '#' && strncmp(href + 1, "rnote", 5) == 0) {
      isNoteref = true;
      Serial.printf("[%lu] [NOTEREF] Detected reverse note pattern: href=%s\n", millis(), href);
    }

    if (isNoteref) {
      Serial.printf("[%lu] [NOTEREF] Found noteref: href=%s\n", millis(), href ? href : "null");

      // Flush any pending word before starting noteref collection
      // This ensures proper word order in the text flow
      if (self->partWordBufferIndex > 0) {
        EpdFontStyle fontStyle = REGULAR;
        if (self->boldUntilDepth < self->depth && self->italicUntilDepth < self->depth) {
          fontStyle = BOLD_ITALIC;
        } else if (self->boldUntilDepth < self->depth) {
          fontStyle = BOLD;
        } else if (self->italicUntilDepth < self->depth) {
          fontStyle = ITALIC;
        }

        self->partWordBuffer[self->partWordBufferIndex] = '\0';
        self->currentTextBlock->addWord(std::move(replaceHtmlEntities(self->partWordBuffer)), fontStyle);
        self->partWordBufferIndex = 0;
      }

      self->insideNoteref = true;
      self->currentNoterefTextLen = 0;
      self->currentNoterefText[0] = '\0';

      if (href) {
        self->currentNoterefHrefLen = 0;
        const char* src = href;
        while (*src && self->currentNoterefHrefLen < 127) {
          self->currentNoterefHref[self->currentNoterefHrefLen++] = *src++;
        }
        self->currentNoterefHref[self->currentNoterefHrefLen] = '\0';
      } else {
        self->currentNoterefHref[0] = '\0';
        self->currentNoterefHrefLen = 0;
      }
      self->depth += 1;
      return;
    }
  }

  if (matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  if (matches(name, SKIP_TAGS, NUM_SKIP_TAGS)) {
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // Skip blocks with role="doc-pagebreak" and epub:type="pagebreak"
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "role") == 0 && strcmp(atts[i + 1], "doc-pagebreak") == 0 ||
          strcmp(atts[i], "epub:type") == 0 && strcmp(atts[i + 1], "pagebreak") == 0) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }
    }
  }

  if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
    self->startNewTextBlock(TextBlock::CENTER_ALIGN);
    self->boldUntilDepth = min(self->boldUntilDepth, self->depth);
  } else if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
    if (strcmp(name, "br") == 0) {
      self->startNewTextBlock(self->currentTextBlock->getStyle());
    } else {
      self->startNewTextBlock(TextBlock::JUSTIFIED);
    }
  } else if (matches(name, BOLD_TAGS, NUM_BOLD_TAGS)) {
    self->boldUntilDepth = min(self->boldUntilDepth, self->depth);
  } else if (matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
    self->italicUntilDepth = min(self->italicUntilDepth, self->depth);
  }

  self->depth += 1;
}

void XMLCALL ChapterHtmlSlimParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Collect paragraph note text in Pass 1
  if (self->insideParagraphNote && self->isPass1CollectingAsides) {
    for (int i = 0; i < len; i++) {
      if (self->currentParagraphNoteTextLen >= self->MAX_PNOTE_BUFFER - 2) {
        if (self->currentParagraphNoteTextLen == self->MAX_PNOTE_BUFFER - 2) {
          Serial.printf("[%lu] [PNOTE] WARNING: Note text truncated at %d chars\n", millis(),
                        self->MAX_PNOTE_BUFFER - 2);
        }
        break;
      }

      unsigned char c = (unsigned char)s[i];

      if (isWhitespace(c)) {
        if (self->currentParagraphNoteTextLen > 0 &&
            self->currentParagraphNoteText[self->currentParagraphNoteTextLen - 1] != ' ') {
          self->currentParagraphNoteText[self->currentParagraphNoteTextLen++] = ' ';
        }
      } else if (c >= 32 || c >= 0x80) {  // Accept printable ASCII AND UTF-8
        self->currentParagraphNoteText[self->currentParagraphNoteTextLen++] = c;
      }
    }
    self->currentParagraphNoteText[self->currentParagraphNoteTextLen] = '\0';
    return;
  }

  // If inside aside, collect the text ONLY in pass 1
  if (self->insideAsideFootnote) {
    if (!self->isPass1CollectingAsides) {
      return;
    }

    for (int i = 0; i < len; i++) {
      if (self->currentAsideTextLen >= self->MAX_ASIDE_BUFFER - 2) {
        if (self->currentAsideTextLen == self->MAX_ASIDE_BUFFER - 2) {
          Serial.printf("[%lu] [ASIDE] WARNING: Footnote text truncated at %d chars (id=%s)\n", millis(),
                        self->MAX_ASIDE_BUFFER - 2, self->currentAsideId);
        }
        break;
      }

      unsigned char c = (unsigned char)s[i];  // Cast to unsigned char

      if (isWhitespace(c)) {
        if (self->currentAsideTextLen > 0 && self->currentAsideText[self->currentAsideTextLen - 1] != ' ') {
          self->currentAsideText[self->currentAsideTextLen++] = ' ';
        }
      } else if (c >= 32 || c >= 0x80) {  // Accept printable ASCII AND UTF-8 bytes
        self->currentAsideText[self->currentAsideTextLen++] = c;
      }
      // Skip control characters (0x00-0x1F) except whitespace
    }
    self->currentAsideText[self->currentAsideTextLen] = '\0';
    return;
  }

  // During pass 1, skip all other content
  if (self->isPass1CollectingAsides) {
    return;
  }

  // Rest of characterData logic for pass 2...
  if (self->insideNoteref) {
    for (int i = 0; i < len; i++) {
      unsigned char c = (unsigned char)s[i];
      // Skip whitespace and brackets []
      if (!isWhitespace(c) && c != '[' && c != ']' && self->currentNoterefTextLen < 15) {
        self->currentNoterefText[self->currentNoterefTextLen++] = c;
        self->currentNoterefText[self->currentNoterefTextLen] = '\0';
      }
    }
    return;
  }

  if (self->skipUntilDepth < self->depth) {
    return;
  }

  EpdFontStyle fontStyle = REGULAR;
  if (self->boldUntilDepth < self->depth && self->italicUntilDepth < self->depth) {
    fontStyle = BOLD_ITALIC;
  } else if (self->boldUntilDepth < self->depth) {
    fontStyle = BOLD;
  } else if (self->italicUntilDepth < self->depth) {
    fontStyle = ITALIC;
  }

  for (int i = 0; i < len; i++) {
    if (isWhitespace(s[i])) {
      if (self->partWordBufferIndex > 0) {
        self->partWordBuffer[self->partWordBufferIndex] = '\0';
        self->currentTextBlock->addWord(std::move(replaceHtmlEntities(self->partWordBuffer)), fontStyle);
        self->partWordBufferIndex = 0;
      }
      continue;
    }

    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      self->partWordBuffer[self->partWordBufferIndex] = '\0';
      self->currentTextBlock->addWord(std::move(replaceHtmlEntities(self->partWordBuffer)), fontStyle);
      self->partWordBufferIndex = 0;
    }

    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }
}

void XMLCALL ChapterHtmlSlimParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Closing paragraph note in Pass 1
  if (strcmp(name, "p") == 0 && self->insideParagraphNote && self->depth - 1 == self->paragraphNoteDepth) {
    if (self->isPass1CollectingAsides && self->currentParagraphNoteTextLen > 0 && self->paragraphNoteCount < 32 &&
        self->currentParagraphNoteId[0] != '\0') {
      // Copy ID
      strncpy(self->paragraphNotes[self->paragraphNoteCount].id, self->currentParagraphNoteId, 15);
      self->paragraphNotes[self->paragraphNoteCount].id[15] = '\0';

      // Allocate memory for text
      size_t textLen = strlen(self->currentParagraphNoteText);
      self->paragraphNotes[self->paragraphNoteCount].text = static_cast<char*>(malloc(textLen + 1));

      if (self->paragraphNotes[self->paragraphNoteCount].text) {
        strcpy(self->paragraphNotes[self->paragraphNoteCount].text, self->currentParagraphNoteText);

        Serial.printf("[%lu] [PNOTE] Stored: %s -> %.80s... (allocated %d bytes)\n", millis(),
                      self->currentParagraphNoteId, self->currentParagraphNoteText, textLen + 1);

        self->paragraphNoteCount++;
      }
    }

    self->insideParagraphNote = false;
    self->depth -= 1;
    return;
  }

  // Closing aside - handle differently for Pass 1 vs Pass 2
  if (strcmp(name, "aside") == 0 && self->insideAsideFootnote && self->depth - 1 == self->asideDepth) {
    // Store footnote ONLY in Pass 1
    if (self->isPass1CollectingAsides && self->currentAsideTextLen > 0 && self->inlineFootnoteCount < 16) {
      // Copy ID (max 2 digits)
      strncpy(self->inlineFootnotes[self->inlineFootnoteCount].id, self->currentAsideId, 2);
      self->inlineFootnotes[self->inlineFootnoteCount].id[2] = '\0';

      // DYNAMIC ALLOCATION: allocate exactly the needed size + 1
      size_t textLen = strlen(self->currentAsideText);
      self->inlineFootnotes[self->inlineFootnoteCount].text = static_cast<char*>(malloc(textLen + 1));

      if (self->inlineFootnotes[self->inlineFootnoteCount].text) {
        strcpy(self->inlineFootnotes[self->inlineFootnoteCount].text, self->currentAsideText);

        Serial.printf("[%lu] [ASIDE] Stored: %s -> %.80s... (allocated %d bytes)\n", millis(), self->currentAsideId,
                      self->currentAsideText, textLen + 1);

        self->inlineFootnoteCount++;
      } else {
        Serial.printf("[%lu] [ASIDE] ERROR: Failed to allocate %d bytes for footnote %s\n", millis(), textLen + 1,
                      self->currentAsideId);
      }
    }

    // Reset state AFTER processing
    self->insideAsideFootnote = false;
    self->depth -= 1;
    return;
  }

  // During pass 1, skip all other processing
  if (self->isPass1CollectingAsides) {
    self->depth -= 1;
    return;
  }

  // Rest of endElement logic for pass 2 - MODIFIED
  if (strcmp(name, "a") == 0 && self->insideNoteref) {
    self->insideNoteref = false;

    if (self->currentNoterefTextLen > 0) {
      Serial.printf("[%lu] [NOTEREF] %s -> %s\n", millis(), self->currentNoterefText, self->currentNoterefHref);

      // Add footnote first (this does the rewriting)
      self->addFootnoteToCurrentPage(self->currentNoterefText, self->currentNoterefHref);

      // Then call callback with the REWRITTEN href from currentPageFootnotes
      if (self->noterefCallback && self->currentPageFootnoteCount > 0) {
        Noteref noteref;
        strncpy(noteref.number, self->currentNoterefText, 15);
        noteref.number[15] = '\0';

        // Use the STORED href which has been rewritten
        FootnoteEntry* lastFootnote = &self->currentPageFootnotes[self->currentPageFootnoteCount - 1];
        strncpy(noteref.href, lastFootnote->href, 127);
        noteref.href[127] = '\0';

        self->noterefCallback(noteref);
      }

      // Ensure [1] appears inline after the word it references
      EpdFontStyle fontStyle = REGULAR;
      if (self->boldUntilDepth < self->depth && self->italicUntilDepth < self->depth) {
        fontStyle = BOLD_ITALIC;
      } else if (self->boldUntilDepth < self->depth) {
        fontStyle = BOLD;
      } else if (self->italicUntilDepth < self->depth) {
        fontStyle = ITALIC;
      }

      // Format the noteref text with brackets
      char formattedNoteref[32];
      snprintf(formattedNoteref, sizeof(formattedNoteref), "[%s]", self->currentNoterefText);

      // Add it as a word to the current text block
      if (self->currentTextBlock) {
        self->currentTextBlock->addWord(formattedNoteref, fontStyle);
      }
    }

    self->currentNoterefTextLen = 0;
    self->currentNoterefText[0] = '\0';
    self->currentNoterefHrefLen = 0;
    self->currentNoterefHref[0] = '\0';
    self->depth -= 1;
    return;
  }

  if (self->partWordBufferIndex > 0) {
    const bool shouldBreakText =
        matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS) || matches(name, HEADER_TAGS, NUM_HEADER_TAGS) ||
        matches(name, BOLD_TAGS, NUM_BOLD_TAGS) || matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS) || self->depth == 1;

    if (shouldBreakText) {
      EpdFontStyle fontStyle = REGULAR;
      if (self->boldUntilDepth < self->depth && self->italicUntilDepth < self->depth) {
        fontStyle = BOLD_ITALIC;
      } else if (self->boldUntilDepth < self->depth) {
        fontStyle = BOLD;
      } else if (self->italicUntilDepth < self->depth) {
        fontStyle = ITALIC;
      }

      self->partWordBuffer[self->partWordBufferIndex] = '\0';
      self->currentTextBlock->addWord(std::move(replaceHtmlEntities(self->partWordBuffer)), fontStyle);
      self->partWordBufferIndex = 0;
    }
  }

  self->depth -= 1;

  if (self->skipUntilDepth == self->depth) {
    self->skipUntilDepth = INT_MAX;
  }

  if (self->boldUntilDepth == self->depth) {
    self->boldUntilDepth = INT_MAX;
  }

  if (self->italicUntilDepth == self->depth) {
    self->italicUntilDepth = INT_MAX;
  }
}

bool ChapterHtmlSlimParser::parseAndBuildPages() {
  // ============================================================================
  // PASS 1: Extract all inline footnotes (aside elements) FIRST
  // ============================================================================
  Serial.printf("[%lu] [PARSER] === PASS 1: Extracting inline footnotes ===\n", millis());

  // Reset state for pass 1
  depth = 0;
  skipUntilDepth = INT_MAX;
  insideAsideFootnote = false;
  insideParagraphNote = false;
  inlineFootnoteCount = 0;
  paragraphNoteCount = 0;
  isPass1CollectingAsides = true;

  XML_Parser parser1 = XML_ParserCreate(nullptr);
  if (!parser1) {
    Serial.printf("[%lu] [EHP] Couldn't allocate memory for parser\n", millis());
    return false;
  }

  XML_SetUserData(parser1, this);
  XML_SetElementHandler(parser1, startElement, endElement);
  XML_SetCharacterDataHandler(parser1, characterData);

  FILE* file = fopen(filepath, "r");
  if (!file) {
    Serial.printf("[%lu] [EHP] Couldn't open file %s\n", millis(), filepath);
    XML_ParserFree(parser1);
    return false;
  }

  int done;
  do {
    void* const buf = XML_GetBuffer(parser1, 1024);
    if (!buf) {
      Serial.printf("[%lu] [EHP] Couldn't allocate memory for buffer\n", millis());
      XML_ParserFree(parser1);
      fclose(file);
      return false;
    }

    const size_t len = fread(buf, 1, 1024, file);

    if (ferror(file)) {
      Serial.printf("[%lu] [EHP] File read error\n", millis());
      XML_ParserFree(parser1);
      fclose(file);
      return false;
    }

    done = feof(file);

    if (XML_ParseBuffer(parser1, static_cast<int>(len), done) == XML_STATUS_ERROR) {
      Serial.printf("[%lu] [EHP] Parse error at line %lu:\n%s\n", millis(), XML_GetCurrentLineNumber(parser1),
                    XML_ErrorString(XML_GetErrorCode(parser1)));
      XML_ParserFree(parser1);
      fclose(file);
      return false;
    }
  } while (!done);

  XML_ParserFree(parser1);
  fclose(file);

  Serial.printf("[%lu] [PARSER] Pass 1 complete: found %d inline footnotes\n", millis(), inlineFootnoteCount);
  for (int i = 0; i < inlineFootnoteCount; i++) {
    Serial.printf("[%lu] [PARSER]   - %s: %.80s\n", millis(), inlineFootnotes[i].id, inlineFootnotes[i].text);
  }

  // ============================================================================
  // PASS 2: Build pages with inline footnotes already available
  // ============================================================================
  Serial.printf("[%lu] [PARSER] === PASS 2: Building pages ===\n", millis());

  // Reset parser state for pass 2
  depth = 0;
  skipUntilDepth = INT_MAX;
  boldUntilDepth = INT_MAX;
  italicUntilDepth = INT_MAX;
  partWordBufferIndex = 0;
  insideNoteref = false;
  insideAsideFootnote = false;
  currentPageFootnoteCount = 0;
  isPass1CollectingAsides = false;

  startNewTextBlock(TextBlock::JUSTIFIED);

  const XML_Parser parser2 = XML_ParserCreate(nullptr);
  if (!parser2) {
    Serial.printf("[%lu] [EHP] Couldn't allocate memory for parser\n", millis());
    return false;
  }

  XML_SetUserData(parser2, this);
  XML_SetElementHandler(parser2, startElement, endElement);
  XML_SetCharacterDataHandler(parser2, characterData);

  file = fopen(filepath, "r");
  if (!file) {
    Serial.printf("[%lu] [EHP] Couldn't open file %s\n", millis(), filepath);
    XML_ParserFree(parser2);
    return false;
  }

  do {
    void* const buf = XML_GetBuffer(parser2, 1024);
    if (!buf) {
      Serial.printf("[%lu] [EHP] Couldn't allocate memory for buffer\n", millis());
      XML_ParserFree(parser2);
      fclose(file);
      return false;
    }

    const size_t len = fread(buf, 1, 1024, file);

    if (ferror(file)) {
      Serial.printf("[%lu] [EHP] File read error\n", millis());
      XML_ParserFree(parser2);
      fclose(file);
      return false;
    }

    done = feof(file);

    if (XML_ParseBuffer(parser2, static_cast<int>(len), done) == XML_STATUS_ERROR) {
      Serial.printf("[%lu] [EHP] Parse error at line %lu:\n%s\n", millis(), XML_GetCurrentLineNumber(parser2),
                    XML_ErrorString(XML_GetErrorCode(parser2)));
      XML_ParserFree(parser2);
      fclose(file);
      return false;
    }
  } while (!done);

  XML_ParserFree(parser2);
  fclose(file);

  // Process last page if there is still text
  if (currentTextBlock) {
    makePages();

    if (currentPage) {
      for (int i = 0; i < currentPageFootnoteCount; i++) {
        currentPage->addFootnote(currentPageFootnotes[i].number, currentPageFootnotes[i].href);
      }
      currentPageFootnoteCount = 0;
      completePageFn(std::move(currentPage));
    }

    currentPage.reset();
    currentTextBlock.reset();
  }

  return true;
}

void ChapterHtmlSlimParser::addLineToPage(std::shared_ptr<TextBlock> line) {
  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  const int pageHeight = GfxRenderer::getScreenHeight() - marginTop - marginBottom;

  if (currentPageNextY + lineHeight > pageHeight) {
    if (currentPage) {
      for (int i = 0; i < currentPageFootnoteCount; i++) {
        currentPage->addFootnote(currentPageFootnotes[i].number, currentPageFootnotes[i].href);
      }
      currentPageFootnoteCount = 0;
    }

    completePageFn(std::move(currentPage));
    currentPage.reset(new Page());
    currentPageNextY = marginTop;
  }

  if (currentPage && currentPage->elementCount < 24) {
    currentPage->addElement(std::make_shared<PageLine>(line, marginLeft, currentPageNextY));
    currentPageNextY += lineHeight;
  } else {
    Serial.printf("[%lu] [EHP] WARNING: Page element capacity reached, skipping element\n", millis());
  }
}

void ChapterHtmlSlimParser::makePages() {
  if (!currentTextBlock) {
    Serial.printf("[%lu] [EHP] !! No text block to make pages for !!\n", millis());
    return;
  }

  if (!currentPage) {
    currentPage.reset(new Page());
    currentPageNextY = marginTop;
  }

  const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
  currentTextBlock->layoutAndExtractLines(
      renderer, fontId, marginLeft + marginRight,
      [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); });
  // Extra paragraph spacing if enabled
  if (extraParagraphSpacing) {
    currentPageNextY += lineHeight / 2;
  }
}
