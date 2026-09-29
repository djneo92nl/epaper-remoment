#include "rss_client.h"

#include <HTTPClient.h>

#include "text_util.h"

static void appendUtf8(String &out, uint32_t cp) {
  if (cp < 0x80) {
    out += (char)cp;
  } else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
  // Anything beyond the BMP (emoji) has no glyph on the panel anyway.
}

// Decodes numeric (&#8217; / &#x2019;) and the common named entities into
// UTF-8 (toDisplayText() later maps that to the panel's font). &amp; goes
// last so "&amp;lt;" becomes the literal text "&lt;", not "<".
static String decodeEntities(const String &in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    if (in[i] == '&' && i + 2 < in.length() && in[i + 1] == '#') {
      int semi = in.indexOf(';', i);
      if (semi > 0 && semi - (int)i <= 10) {
        bool hex = in[i + 2] == 'x' || in[i + 2] == 'X';
        String digits = in.substring(i + (hex ? 3 : 2), semi);
        uint32_t cp = strtoul(digits.c_str(), nullptr, hex ? 16 : 10);
        if (cp > 0) {
          appendUtf8(out, cp);
          i = semi;
          continue;
        }
      }
    }
    out += in[i];
  }
  out.replace("&quot;", "\"");
  out.replace("&apos;", "'");
  out.replace("&nbsp;", " ");
  out.replace("&lt;", "<");
  out.replace("&gt;", ">");
  out.replace("&amp;", "&");
  return out;
}

static String stripHtmlTags(const String &in) {
  String out;
  bool inTag = false;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '<') {
      inTag = true;
    } else if (c == '>') {
      inTag = false;
    } else if (!inTag) {
      out += c;
    }
  }
  return out;
}

// Feed content (this NOS feed especially) often has embedded newlines and
// indentation from the source XML's formatting. A literal '\n' reaching
// display.print() makes Adafruit GFX reset the cursor to x=0 mid-line --
// which lands in the *other* column, not just at the start of a new line
// in the same one. Collapse all whitespace runs to single spaces so no
// raw '\n'/'\r'/'\t' ever reaches the renderer.
static String collapseWhitespace(const String &in) {
  String out;
  bool lastWasSpace = false;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    bool isSpace = (c == ' ' || c == '\n' || c == '\r' || c == '\t');
    if (isSpace) {
      if (!lastWasSpace && out.length() > 0) {
        out += ' ';
      }
      lastWasSpace = true;
    } else {
      out += c;
      lastWasSpace = false;
    }
  }
  while (out.length() > 0 && out[out.length() - 1] == ' ') {
    out.remove(out.length() - 1);
  }
  return out;
}

// Extracts the content of <tagName>...</tagName> (plain or CDATA-wrapped),
// searching forward from `searchFrom`. Sets nextSearchPos to just past the
// closing tag (or xml.length() if not found).
static String extractTagAt(const String &xml, const String &openTag, const String &closeTag,
                            int searchFrom, int &nextSearchPos) {
  int openPos = xml.indexOf(openTag, searchFrom);
  if (openPos < 0) {
    nextSearchPos = xml.length();
    return "";
  }
  int contentStart = openPos + openTag.length();

  if (xml.startsWith("<![CDATA[", contentStart)) {
    int cdataStart = contentStart + 9; // strlen("<![CDATA[")
    int cdataEnd = xml.indexOf("]]>", cdataStart);
    if (cdataEnd < 0) {
      nextSearchPos = xml.length();
      return "";
    }
    String content = xml.substring(cdataStart, cdataEnd);
    int closeTagPos = xml.indexOf(closeTag, cdataEnd);
    nextSearchPos = (closeTagPos < 0) ? xml.length() : closeTagPos + closeTag.length();
    return collapseWhitespace(decodeEntities(stripHtmlTags(content)));
  }

  int closeTagPos = xml.indexOf(closeTag, contentStart);
  if (closeTagPos < 0) {
    nextSearchPos = xml.length();
    return "";
  }
  nextSearchPos = closeTagPos + closeTag.length();
  // Outside CDATA any HTML is itself entity-escaped ("&lt;p&gt;"):
  // unescape it first so the tags can be stripped, then decode what was
  // inside them.
  String raw = xml.substring(contentStart, closeTagPos);
  return collapseWhitespace(decodeEntities(stripHtmlTags(decodeEntities(raw))));
}

// Upper bound on how much of the first <item> we buffer. NOS/Tweakers items
// are ~1-2KB; this only guards against a feed without a closing tag.
static const size_t MAX_ITEM_BYTES = 12000;
static const unsigned long READ_TIMEOUT_MS = 15000;

int rssFetchLatest(const char *url, RssHeadline &out) {
  HTTPClient http;
  // HTTP/1.0 rules out chunked encoding, so the raw stream below is just
  // the XML body.
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("epaper-remoment/1.0");
  http.begin(url);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[rss] fetch failed, HTTP %d\n", code);
    http.end();
    return -1;
  }

  // Stream the feed and keep only the first <item>...</item>. This used to
  // getString() the whole feed: the NOS feed alone is ~70KB, and allocating
  // that on top of the TLS buffers regularly failed, which is what kept
  // showing "News unavailable".
  auto *stream = http.getStreamPtr();
  String buf;
  bool inItem = false;
  bool itemClosed = false;
  uint8_t chunk[256];
  unsigned long start = millis();

  while ((stream->connected() || stream->available()) && millis() - start < READ_TIMEOUT_MS) {
    size_t avail = stream->available();
    if (avail == 0) {
      delay(5);
      continue;
    }
    int n = stream->read(chunk, min(avail, sizeof(chunk)));
    if (n <= 0) {
      continue;
    }
    buf.concat((const char *)chunk, n);

    if (!inItem) {
      int itemPos = buf.indexOf("<item");
      if (itemPos >= 0) {
        buf.remove(0, itemPos);
        inItem = true;
      } else if (buf.length() > 16) {
        // Keep a small tail in case "<item" straddles two chunks.
        buf.remove(0, buf.length() - 16);
      }
    }
    if (inItem && (buf.indexOf("</item>") >= 0 || buf.length() > MAX_ITEM_BYTES)) {
      itemClosed = true;
      break;
    }
  }
  http.end();

  if (!inItem) {
    Serial.println("[rss] no <item> found in feed");
    return 0;
  }
  if (!itemClosed) {
    Serial.println("[rss] feed ended or timed out mid-item, using what we have");
  }

  int dummy;
  out.title = toDisplayText(extractTagAt(buf, "<title>", "</title>", 0, dummy));
  out.description = toDisplayText(extractTagAt(buf, "<description>", "</description>", 0, dummy));

  if (out.title.length() == 0) {
    return 0;
  }
  return 1;
}
