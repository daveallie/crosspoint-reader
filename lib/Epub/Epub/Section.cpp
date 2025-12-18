#include "Section.h"

#include <SD.h>
#include <Serialization.h>

#include <fstream>
#include <set>

#include "FsHelpers.h"
#include "Page.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 5;
}

// Helper function to write XML-escaped text directly to file
static bool writeEscapedXml(File& file, const char* text) {
  if (!text) return true;

  // Use a static buffer to avoid heap allocation
  static char buffer[2048];
  int bufferPos = 0;

  while (*text && bufferPos < sizeof(buffer) - 10) {  // Leave margin for entities
    unsigned char c = (unsigned char)*text;

    // Only escape the 5 XML special characters
    if (c == '<') {
      if (bufferPos + 4 < sizeof(buffer)) {
        memcpy(&buffer[bufferPos], "&lt;", 4);
        bufferPos += 4;
      }
    } else if (c == '>') {
      if (bufferPos + 4 < sizeof(buffer)) {
        memcpy(&buffer[bufferPos], "&gt;", 4);
        bufferPos += 4;
      }
    } else if (c == '&') {
      if (bufferPos + 5 < sizeof(buffer)) {
        memcpy(&buffer[bufferPos], "&amp;", 5);
        bufferPos += 5;
      }
    } else if (c == '"') {
      if (bufferPos + 6 < sizeof(buffer)) {
        memcpy(&buffer[bufferPos], "&quot;", 6);
        bufferPos += 6;
      }
    } else if (c == '\'') {
      if (bufferPos + 6 < sizeof(buffer)) {
        memcpy(&buffer[bufferPos], "&apos;", 6);
        bufferPos += 6;
      }
    } else {
      // Keep everything else (include UTF8)
      // This preserves accented characters like é, è, à, etc.
      buffer[bufferPos++] = (char)c;
    }

    text++;
  }

  buffer[bufferPos] = '\0';

  // Write all at once
  size_t written = file.write((const uint8_t*)buffer, bufferPos);
  file.flush();

  return written == bufferPos;
}

void Section::onPageComplete(std::unique_ptr<Page> page) {
  const auto filePath = cachePath + "/page_" + std::to_string(pageCount) + ".bin";

  std::ofstream outputFile("/sd" + filePath);
  page->serialize(outputFile);
  outputFile.close();

  Serial.printf("[%lu] [SCT] Page %d processed\n", millis(), pageCount);

  pageCount++;
}

void Section::writeCacheMetadata(const int fontId, const float lineCompression, const int marginTop,
                                 const int marginRight, const int marginBottom, const int marginLeft,
                                 const bool extraParagraphSpacing) const {
  std::ofstream outputFile(("/sd" + cachePath + "/section.bin").c_str());
  serialization::writePod(outputFile, SECTION_FILE_VERSION);
  serialization::writePod(outputFile, fontId);
  serialization::writePod(outputFile, lineCompression);
  serialization::writePod(outputFile, marginTop);
  serialization::writePod(outputFile, marginRight);
  serialization::writePod(outputFile, marginBottom);
  serialization::writePod(outputFile, marginLeft);
  serialization::writePod(outputFile, extraParagraphSpacing);
  serialization::writePod(outputFile, pageCount);
  outputFile.close();
}

bool Section::loadCacheMetadata(const int fontId, const float lineCompression, const int marginTop,
                                const int marginRight, const int marginBottom, const int marginLeft,
                                const bool extraParagraphSpacing) {
  if (!SD.exists(cachePath.c_str())) {
    return false;
  }

  const auto sectionFilePath = cachePath + "/section.bin";
  if (!SD.exists(sectionFilePath.c_str())) {
    return false;
  }

  std::ifstream inputFile(("/sd" + sectionFilePath).c_str());

  // Match parameters
  {
    uint8_t version;
    serialization::readPod(inputFile, version);
    if (version != SECTION_FILE_VERSION) {
      inputFile.close();
      Serial.printf("[%lu] [SCT] Deserialization failed: Unknown version %u\n", millis(), version);
      clearCache();
      return false;
    }

    int fileFontId, fileMarginTop, fileMarginRight, fileMarginBottom, fileMarginLeft;
    float fileLineCompression;
    bool fileExtraParagraphSpacing;
    serialization::readPod(inputFile, fileFontId);
    serialization::readPod(inputFile, fileLineCompression);
    serialization::readPod(inputFile, fileMarginTop);
    serialization::readPod(inputFile, fileMarginRight);
    serialization::readPod(inputFile, fileMarginBottom);
    serialization::readPod(inputFile, fileMarginLeft);
    serialization::readPod(inputFile, fileExtraParagraphSpacing);

    if (fontId != fileFontId || lineCompression != fileLineCompression || marginTop != fileMarginTop ||
        marginRight != fileMarginRight || marginBottom != fileMarginBottom || marginLeft != fileMarginLeft ||
        extraParagraphSpacing != fileExtraParagraphSpacing) {
      inputFile.close();
      Serial.printf("[%lu] [SCT] Deserialization failed: Parameters do not match\n", millis());
      clearCache();
      return false;
    }
  }

  serialization::readPod(inputFile, pageCount);
  inputFile.close();
  Serial.printf("[%lu] [SCT] Deserialization succeeded: %d pages\n", millis(), pageCount);
  return true;
}

void Section::setupCacheDir() const {
  epub->setupCacheDir();
  SD.mkdir(cachePath.c_str());
}

bool Section::clearCache() const {
  if (!SD.exists(cachePath.c_str())) {
    Serial.printf("[%lu] [SCT] Cache does not exist, no action needed\n", millis());
    return true;
  }

  if (!FsHelpers::removeDir(cachePath.c_str())) {
    Serial.printf("[%lu] [SCT] Failed to clear cache\n", millis());
    return false;
  }

  Serial.printf("[%lu] [SCT] Cache cleared successfully\n", millis());
  return true;
}

bool Section::persistPageDataToSD(const int fontId, const float lineCompression, const int marginTop,
                                  const int marginRight, const int marginBottom, const int marginLeft,
                                  const bool extraParagraphSpacing) {
  const auto localPath = epub->getSpineItem(spineIndex);

  // Check if it's a virtual spine item
  if (epub->isVirtualSpineItem(spineIndex)) {
    Serial.printf("[%lu] [SCT] Processing virtual spine item: %s\n", millis(), localPath.c_str());

    const auto sdPath = "/sd" + localPath;

    ChapterHtmlSlimParser visitor(
        sdPath.c_str(), renderer, fontId, lineCompression, marginTop, marginRight, marginBottom, marginLeft,
        extraParagraphSpacing, [this](std::unique_ptr<Page> page) { this->onPageComplete(std::move(page)); },
        cachePath);

    bool success = visitor.parseAndBuildPages();

    if (!success) {
      Serial.printf("[%lu] [SCT] Failed to parse virtual file\n", millis());
      return false;
    }

    writeCacheMetadata(fontId, lineCompression, marginTop, marginRight, marginBottom, marginLeft,
                       extraParagraphSpacing);
    return true;
  }

  // Normal file
  const auto tmpHtmlPath = epub->getCachePath() + "/.tmp_" + std::to_string(spineIndex) + ".html";
  File f = SD.open(tmpHtmlPath.c_str(), FILE_WRITE, true);
  bool success = epub->readItemContentsToStream(localPath, f, 1024);
  f.close();

  if (!success) {
    Serial.printf("[%lu] [SCT] Failed to stream item contents to temp file\n", millis());
    return false;
  }

  Serial.printf("[%lu] [SCT] Streamed temp HTML to %s\n", millis(), tmpHtmlPath.c_str());

  const auto sdTmpHtmlPath = "/sd" + tmpHtmlPath;

  ChapterHtmlSlimParser visitor(
      sdTmpHtmlPath.c_str(), renderer, fontId, lineCompression, marginTop, marginRight, marginBottom, marginLeft,
      extraParagraphSpacing, [this](std::unique_ptr<Page> page) { this->onPageComplete(std::move(page)); }, cachePath);

  // Track which inline footnotes AND paragraph notes are actually referenced in this file
  std::set<std::string> rewrittenInlineIds;
  int noterefCount = 0;

  visitor.setNoterefCallback([this, &noterefCount, &rewrittenInlineIds](Noteref& noteref) {
    Serial.printf("[%lu] [SCT] Callback noteref: %s -> %s\n", millis(), noteref.number, noteref.href);

    // Extract the ID from the href for tracking
    std::string href(noteref.href);

    // Check if this was rewritten to an inline or paragraph note
    if (href.find("inline_") == 0 || href.find("pnote_") == 0) {
      size_t underscorePos = href.find('_');
      size_t dotPos = href.find('.');

      if (underscorePos != std::string::npos && dotPos != std::string::npos) {
        std::string noteId = href.substr(underscorePos + 1, dotPos - underscorePos - 1);
        rewrittenInlineIds.insert(noteId);
        Serial.printf("[%lu] [SCT] Marked note as rewritten: %s\n", millis(), noteId.c_str());
      }
    } else {
      // Normal external footnote
      epub->markAsFootnotePage(noteref.href);
    }

    noterefCount++;
  });

  // Parse and build pages (inline hrefs are rewritten automatically inside parser)
  success = visitor.parseAndBuildPages();

  SD.remove(tmpHtmlPath.c_str());

  if (!success) {
    Serial.printf("[%lu] [SCT] Failed to parse XML and build pages\n", millis());
    return false;
  }

  // NOW generate inline footnote HTML files ONLY for rewritten ones
  Serial.printf("[%lu] [SCT] Found %d inline footnotes, %d were referenced\n", millis(), visitor.inlineFootnoteCount,
                rewrittenInlineIds.size());

  for (int i = 0; i < visitor.inlineFootnoteCount; i++) {
    const char* inlineId = visitor.inlineFootnotes[i].id;
    const char* inlineText = visitor.inlineFootnotes[i].text;

    // Only generate if this inline footnote was actually referenced
    if (rewrittenInlineIds.find(std::string(inlineId)) == rewrittenInlineIds.end()) {
      Serial.printf("[%lu] [SCT] Skipping unreferenced inline footnote: %s\n", millis(), inlineId);
      continue;
    }

    // Verify that the text exists
    if (!inlineText || strlen(inlineText) == 0) {
      Serial.printf("[%lu] [SCT] Skipping empty inline footnote: %s\n", millis(), inlineId);
      continue;
    }

    Serial.printf("[%lu] [SCT] Processing inline footnote: %s (len=%d)\n", millis(), inlineId, strlen(inlineText));

    char inlineFilename[64];
    snprintf(inlineFilename, sizeof(inlineFilename), "inline_%s.html", inlineId);

    // Store in main cache dir, not section cache dir
    std::string fullPath = epub->getCachePath() + "/" + std::string(inlineFilename);

    Serial.printf("[%lu] [SCT] Generating inline file: %s\n", millis(), fullPath.c_str());

    File file = SD.open(fullPath.c_str(), FILE_WRITE, true);
    if (file) {
      // valid XML declaration and encoding
      file.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
      file.println("<!DOCTYPE html>");
      file.println("<html xmlns=\"http://www.w3.org/1999/xhtml\">");
      file.println("<head>");
      file.println("<meta charset=\"UTF-8\"/>");
      file.println("<title>Footnote</title>");
      file.println("</head>");
      file.println("<body>");

      // Paragraph with content
      file.print("<p id=\"");
      file.print(inlineId);
      file.print("\">");

      if (!writeEscapedXml(file, inlineText)) {
        Serial.printf("[%lu] [SCT] Warning: writeEscapedXml may have failed\n", millis());
      }

      file.println("</p>");
      file.println("</body>");
      file.println("</html>");
      file.close();

      Serial.printf("[%lu] [SCT] Generated inline footnote file\n", millis());

      int virtualIndex = epub->addVirtualSpineItem(fullPath);
      Serial.printf("[%lu] [SCT] Added virtual spine item at index %d\n", millis(), virtualIndex);

      // Mark as footnote page
      char newHref[128];
      snprintf(newHref, sizeof(newHref), "%s#%s", inlineFilename, inlineId);
      epub->markAsFootnotePage(newHref);
    } else {
      Serial.printf("[%lu] [SCT] Failed to create inline file\n", millis());
    }
  }

  // Generate paragraph note HTML files
  Serial.printf("[%lu] [SCT] Found %d paragraph notes\n", millis(), visitor.paragraphNoteCount);

  for (int i = 0; i < visitor.paragraphNoteCount; i++) {
    const char* pnoteId = visitor.paragraphNotes[i].id;
    const char* pnoteText = visitor.paragraphNotes[i].text;

    if (!pnoteText || strlen(pnoteText) == 0) {
      continue;
    }

    // Check if this paragraph note was referenced
    if (rewrittenInlineIds.find(std::string(pnoteId)) == rewrittenInlineIds.end()) {
      Serial.printf("[%lu] [SCT] Skipping unreferenced paragraph note: %s\n", millis(), pnoteId);
      continue;
    }

    // Create filename: pnote_rnote1.html
    char pnoteFilename[64];
    snprintf(pnoteFilename, sizeof(pnoteFilename), "pnote_%s.html", pnoteId);

    std::string fullPath = epub->getCachePath() + "/" + std::string(pnoteFilename);

    Serial.printf("[%lu] [SCT] Generating paragraph note file: %s\n", millis(), fullPath.c_str());

    File file = SD.open(fullPath.c_str(), FILE_WRITE, true);
    if (file) {
      file.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
      file.println("<!DOCTYPE html>");
      file.println("<html xmlns=\"http://www.w3.org/1999/xhtml\">");
      file.println("<head>");
      file.println("<meta charset=\"UTF-8\"/>");
      file.println("<title>Note</title>");
      file.println("</head>");
      file.println("<body>");
      file.print("<p id=\"");
      file.print(pnoteId);
      file.print("\">");

      if (!writeEscapedXml(file, pnoteText)) {
        Serial.printf("[%lu] [SCT] Warning: writeEscapedXml may have failed\n", millis());
      }

      file.println("</p>");
      file.println("</body>");
      file.println("</html>");
      file.close();

      Serial.printf("[%lu] [SCT] Generated paragraph note file\n", millis());

      int virtualIndex = epub->addVirtualSpineItem(fullPath);
      Serial.printf("[%lu] [SCT] Added virtual spine item at index %d\n", millis(), virtualIndex);

      char newHref[128];
      snprintf(newHref, sizeof(newHref), "%s#%s", pnoteFilename, pnoteId);
      epub->markAsFootnotePage(newHref);
    }
  }

  Serial.printf("[%lu] [SCT] Total noterefs found: %d\n", millis(), noterefCount);

  writeCacheMetadata(fontId, lineCompression, marginTop, marginRight, marginBottom, marginLeft, extraParagraphSpacing);

  return true;
}

std::unique_ptr<Page> Section::loadPageFromSD() const {
  const auto filePath = "/sd" + cachePath + "/page_" + std::to_string(currentPage) + ".bin";
  if (!SD.exists(filePath.c_str() + 3)) {
    Serial.printf("[%lu] [SCT] Page file does not exist: %s\n", millis(), filePath.c_str());
    return nullptr;
  }

  std::ifstream inputFile(filePath);
  auto page = Page::deserialize(inputFile);
  inputFile.close();
  return page;
}
