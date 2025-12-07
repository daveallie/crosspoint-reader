#include "Epub.h"

#include <HardwareSerial.h>
#include <SD.h>
#include <ZipFile.h>

#include <map>

bool Epub::findContentOpfFile(const ZipFile& zip, std::string& contentOpfFile) {
  // open up the meta data to find where the content.opf file lives
  size_t s;
  const auto metaInfo = reinterpret_cast<char*>(zip.readFileToMemory("META-INF/container.xml", &s, true));
  if (!metaInfo) {
    Serial.println("Could not find META-INF/container.xml");
    return false;
  }

  // parse the meta data
  tinyxml2::XMLDocument metaDataDoc;
  const auto result = metaDataDoc.Parse(metaInfo);
  free(metaInfo);

  if (result != tinyxml2::XML_SUCCESS) {
    Serial.printf("Could not parse META-INF/container.xml. Error: %d\n", result);
    return false;
  }

  const auto container = metaDataDoc.FirstChildElement("container");
  if (!container) {
    Serial.println("Could not find container element in META-INF/container.xml");
    return false;
  }

  const auto rootfiles = container->FirstChildElement("rootfiles");
  if (!rootfiles) {
    Serial.println("Could not find rootfiles element in META-INF/container.xml");
    return false;
  }

  // find the root file that has the media-type="application/oebps-package+xml"
  auto rootfile = rootfiles->FirstChildElement("rootfile");
  while (rootfile) {
    const char* mediaType = rootfile->Attribute("media-type");
    if (mediaType && strcmp(mediaType, "application/oebps-package+xml") == 0) {
      const char* full_path = rootfile->Attribute("full-path");
      if (full_path) {
        contentOpfFile = full_path;
        return true;
      }
    }
    rootfile = rootfile->NextSiblingElement("rootfile");
  }

  Serial.println("Could not get path to content.opf file");
  return false;
}

bool Epub::parseContentOpf(ZipFile& zip, std::string& content_opf_file) {
  // read in the content.opf file and parse it
  auto contents = reinterpret_cast<char*>(zip.readFileToMemory(content_opf_file.c_str(), nullptr, true));

  // parse the contents
  tinyxml2::XMLDocument doc;
  auto result = doc.Parse(contents);
  free(contents);

  if (result != tinyxml2::XML_SUCCESS) {
    Serial.printf("Error parsing content.opf - %s\n", tinyxml2::XMLDocument::ErrorIDToName(result));
    return false;
  }

  auto package = doc.FirstChildElement("package");
  if (!package) package = doc.FirstChildElement("opf:package");

  if (!package) {
    Serial.println("Could not find package element in content.opf");
    return false;
  }

  // get the metadata - title and cover image
  auto metadata = package->FirstChildElement("metadata");
  if (!metadata) metadata = package->FirstChildElement("opf:metadata");
  if (!metadata) {
    Serial.println("Missing metadata");
    return false;
  }

  auto titleEl = metadata->FirstChildElement("dc:title");
  if (!titleEl) {
    Serial.println("Missing title");
    return false;
  }
  this->title = titleEl->GetText();

  auto cover = metadata->FirstChildElement("meta");
  if (!cover) cover = metadata->FirstChildElement("opf:meta");
  while (cover && cover->Attribute("name") && strcmp(cover->Attribute("name"), "cover") != 0) {
    cover = cover->NextSiblingElement("meta");
  }
  if (!cover) {
    Serial.println("Missing cover");
  }
  auto coverItem = cover ? cover->Attribute("content") : nullptr;

  // read the manifest and spine
  // the manifest gives us the names of the files
  // the spine gives us the order of the files
  // we can then read the files in the order they are in the spine
  auto manifest = package->FirstChildElement("manifest");
  if (!manifest) manifest = package->FirstChildElement("opf:manifest");
  if (!manifest) {
    Serial.println("Missing manifest");
    return false;
  }

  // create a mapping from id to file name
  auto item = manifest->FirstChildElement("item");
  if (!item) item = manifest->FirstChildElement("opf:item");
  std::map<std::string, std::string> items;

  while (item) {
    std::string itemId = item->Attribute("id");
    std::string href = contentBasePath + item->Attribute("href");

    // grab the cover image
    if (coverItem && itemId == coverItem) {
      coverImageItem = href;
    }

    // grab the ncx file
    if (itemId == "ncx" || itemId == "ncxtoc") {
      tocNcxItem = href;
    }

    items[itemId] = href;
    auto nextItem = item->NextSiblingElement("item");
    if (!nextItem) nextItem = item->NextSiblingElement("opf:item");
    item = nextItem;
  }

  // find the spine
  auto spineEl = package->FirstChildElement("spine");
  if (!spineEl) spineEl = package->FirstChildElement("opf:spine");
  if (!spineEl) {
    Serial.println("Missing spine");
    return false;
  }

  // read the spine
  auto itemref = spineEl->FirstChildElement("itemref");
  if (!itemref) itemref = spineEl->FirstChildElement("opf:itemref");
  while (itemref) {
    auto id = itemref->Attribute("idref");
    if (items.find(id) != items.end()) {
      spine.emplace_back(id, items[id]);
    }
    auto nextItemRef = itemref->NextSiblingElement("itemref");
    if (!nextItemRef) nextItemRef = itemref->NextSiblingElement("opf:itemref");
    itemref = nextItemRef;
  }
  return true;
}

bool Epub::parseTocNcxFile(const ZipFile& zip) {
  // the ncx file should have been specified in the content.opf file
  if (tocNcxItem.empty()) {
    Serial.println("No ncx file specified");
    return false;
  }

  const auto ncxData = reinterpret_cast<char*>(zip.readFileToMemory(tocNcxItem.c_str(), nullptr, true));
  if (!ncxData) {
    Serial.printf("Could not find %s\n", tocNcxItem.c_str());
    return false;
  }

  // Parse the Toc contents
  tinyxml2::XMLDocument doc;
  const auto result = doc.Parse(ncxData);
  free(ncxData);

  if (result != tinyxml2::XML_SUCCESS) {
    Serial.printf("Error parsing toc %s\n", tinyxml2::XMLDocument::ErrorIDToName(result));
    return false;
  }

  const auto ncx = doc.FirstChildElement("ncx");
  if (!ncx) {
    Serial.println("Could not find first child ncx in toc");
    return false;
  }

  const auto navMap = ncx->FirstChildElement("navMap");
  if (!navMap) {
    Serial.println("Could not find navMap child in ncx");
    return false;
  }

  recursivelyParseNavMap(navMap->FirstChildElement("navPoint"));
  return true;
}

void Epub::recursivelyParseNavMap(tinyxml2::XMLElement* element) {
  // Fills toc map
  while (element) {
    std::string navTitle = element->FirstChildElement("navLabel")->FirstChildElement("text")->FirstChild()->Value();
    const auto content = element->FirstChildElement("content");
    std::string href = contentBasePath + content->Attribute("src");
    // split the href on the # to get the href and the anchor
    const size_t pos = href.find('#');
    std::string anchor;

    if (pos != std::string::npos) {
      anchor = href.substr(pos + 1);
      href = href.substr(0, pos);
    }

    toc.emplace_back(navTitle, href, anchor, 0);

    tinyxml2::XMLElement* nestedNavPoint = element->FirstChildElement("navPoint");
    if (nestedNavPoint) {
      recursivelyParseNavMap(nestedNavPoint);
    }
    element = element->NextSiblingElement("navPoint");
  }
}

// load in the meta data for the epub file
bool Epub::load() {
  ZipFile zip("/sd" + filepath);

  std::string contentOpfFile;
  if (!findContentOpfFile(zip, contentOpfFile)) {
    Serial.println("Could not open ePub");
    return false;
  }

  contentBasePath = contentOpfFile.substr(0, contentOpfFile.find_last_of('/') + 1);

  if (!parseContentOpf(zip, contentOpfFile)) {
    return false;
  }

  if (!parseTocNcxFile(zip)) {
    return false;
  }

  return true;
}

void Epub::clearCache() const { SD.rmdir(cachePath.c_str()); }

void Epub::setupCacheDir() const {
  if (SD.exists(cachePath.c_str())) {
    return;
  }

  // Loop over each segment of the cache path and create directories as needed
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') {
      SD.mkdir(cachePath.substr(0, i).c_str());
    }
  }
  SD.mkdir(cachePath.c_str());
}

const std::string& Epub::getCachePath() const { return cachePath; }

const std::string& Epub::getPath() const { return filepath; }

const std::string& Epub::getTitle() const { return title; }

const std::string& Epub::getCoverImageItem() const { return coverImageItem; }

std::string normalisePath(const std::string& path) {
  std::vector<std::string> components;
  std::string component;

  for (const auto c : path) {
    if (c == '/') {
      if (!component.empty()) {
        if (component == "..") {
          if (!components.empty()) {
            components.pop_back();
          }
        } else {
          components.push_back(component);
        }
        component.clear();
      }
    } else {
      component += c;
    }
  }

  if (!component.empty()) {
    components.push_back(component);
  }

  std::string result;
  for (const auto& c : components) {
    if (!result.empty()) {
      result += "/";
    }
    result += c;
  }

  return result;
}

uint8_t* Epub::readItemContentsToBytes(const std::string& itemHref, size_t* size, bool trailingNullByte) const {
  const ZipFile zip("/sd" + filepath);
  const std::string path = normalisePath(itemHref);

  const auto content = zip.readFileToMemory(path.c_str(), size, trailingNullByte);
  if (!content) {
    Serial.printf("Failed to read item %s\n", path.c_str());
    return nullptr;
  }

  return content;
}

bool Epub::readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize) const {
  const ZipFile zip("/sd" + filepath);
  const std::string path = normalisePath(itemHref);

  return zip.readFileToStream(path.c_str(), out, chunkSize);
}

int Epub::getSpineItemsCount() const { return spine.size(); }

std::string& Epub::getSpineItem(const int spineIndex) {
  if (spineIndex < 0 || spineIndex >= spine.size()) {
    Serial.printf("getSpineItem index:%d is out of range\n", spineIndex);
    return spine.at(0).second;
  }

  return spine.at(spineIndex).second;
}

EpubTocEntry& Epub::getTocItem(const int tocTndex) {
  if (tocTndex < 0 || tocTndex >= toc.size()) {
    Serial.printf("getTocItem index:%d is out of range\n", tocTndex);
    return toc.at(0);
  }

  return toc.at(tocTndex);
}

int Epub::getTocItemsCount() const { return toc.size(); }

// work out the section index for a toc index
int Epub::getSpineIndexForTocIndex(const int tocIndex) const {
  // the toc entry should have an href that matches the spine item
  // so we can find the spine index by looking for the href
  for (int i = 0; i < spine.size(); i++) {
    if (spine[i].second == toc[tocIndex].href) {
      return i;
    }
  }

  Serial.println("Section not found");
  // not found - default to the start of the book
  return 0;
}

int Epub::getTocIndexForSpineIndex(const int spineIndex) const {
  // the toc entry should have an href that matches the spine item
  // so we can find the toc index by looking for the href
  for (int i = 0; i < toc.size(); i++) {
    if (toc[i].href == spine[spineIndex].second) {
      return i;
    }
  }

  Serial.println("TOC item not found");
  // not found - default to first item
  return 0;
}
